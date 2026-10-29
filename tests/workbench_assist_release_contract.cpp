// Contract for the assist release barrier (T35-02).
// The worker is blocked inside IToolInstance::release() by a latch instead of a sleep, so the test
// observes the exact window in which the GUI must keep busy() true: no drain or close may proceed
// until the worker acknowledges the release it actually performed.
#include "core/Error.h"
#include "core/ToolRegistry.h"
#include "ui/AssistPreview.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace visionrt;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Shared latch: release() reports that it started, then waits until the test opens the gate.
struct ReleaseGate
{
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    open    = false;
    bool                    started = false;
    std::atomic<int>        releases{0};

    void reset()
    {
        std::lock_guard lock(mutex);
        open    = false;
        started = false;
    }

    void allow()
    {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        cv.notify_all();
    }

    bool waitStarted(int milliseconds = 2000)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return started; });
    }
};

ReleaseGate gate;

class ExecuteContractErrorTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &) override { return {}; }
    ToolResult execute(const Values &) override
    {
        throw ContractError("辅助执行输出违反契约");
    }
    ToolStatus release() override { return {}; }
};

class UnexpectedThrowingTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &) override { return {}; }
    ToolResult execute(const Values &) override
    {
        throw std::runtime_error("辅助工具未捕获标准异常");
    }
    ToolStatus release() override { return {}; }
};

class LatchedReleaseTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolResult execute(const Values &) override
    {
        // The two status ports are the host's to materialise after a successful run; a tool that
        // writes them is a contract error, so this fixture only reports its own business ports.
        Values outputs;
        outputs.emplace("valid", false);
        outputs.emplace("quality", std::string{});
        outputs.emplace("preview", DataRef<ROI>(ROI{}));
        outputs.emplace("proposal", DataRef<ROI>(ROI{}));
        return toolResult(std::move(outputs), {0, "运行成功"});
    }

    ToolStatus release() override
    {
        {
            std::unique_lock lock(gate.mutex);
            gate.started = true;
            gate.cv.notify_all();
            gate.cv.wait(lock, [] { return gate.open; });
        }
        ++gate.releases;
        return {};
    }
};

ToolDefinition helperDefinition()
{
    ToolDefinition definition;
    definition.id          = "test.assist.release";
    definition.displayName = "释放闩锁辅助";
    definition.version     = "1";
    definition.kind        = ToolKind::Cpp;
    definition.outputs     = {{"valid", "有效", {Type::Bool}},
                              {"quality", "质量", {Type::String}},
                              {"preview", "预览", {Type::ROI}},
                              {"proposal", "坐标", {Type::ROI}}};
    return definition;
}

void settle(AssistPreview &preview, int milliseconds = 5000)
{
    require(QTest::qWaitFor([&] { return !preview.busy(); }, milliseconds), "assist preview stayed busy");
}

// One learned request creates the persistent instance the release barrier owns.
void learnOnce(AssistPreview &preview)
{
    AssistRequest request;
    request.learn = true;
    require(preview.request(request), "learn request was rejected");
    settle(preview);
}

} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);

    const auto definition = helperDefinition();
    auto       registry   = std::make_shared<ToolRegistry>();
    registry->registerCpp(definition, [](const ToolServices &) { return std::make_unique<LatchedReleaseTool>(); });
    registry->addDefinition(definition);

    AssistPreview preview;
    preview.configure(registry, definition, {});
    learnOnce(preview);

    // A refresh while the worker is still destroying the instance must keep the barrier closed:
    // busy() is the only thing a drain consults, so it may not drop before the acknowledgement.
    gate.reset();
    preview.releaseSession();
    require(preview.busy(), "release did not close the barrier");
    require(gate.waitStarted(), "worker never entered release");
    QTest::qWait(120);
    require(preview.busy(), "barrier opened while release was still running");
    require(gate.releases.load() == 0, "release reported completion before it finished");

    gate.allow();
    require(QTest::qWaitFor([&] { return !preview.busy(); }, 5000), "release acknowledgement never arrived");
    require(gate.releases.load() == 1, "release was performed more than once for one request");

    // A reset/rescan after the release asks again: a fresh instance needs exactly one new release.
    learnOnce(preview);
    gate.reset();
    preview.releaseSession();
    require(gate.waitStarted(), "second release never started");
    gate.allow();
    require(QTest::qWaitFor([&] { return !preview.busy(); }, 5000), "second release was not acknowledged");
    require(gate.releases.load() == 2, "second release count is wrong");

    // Close: beginShutdown must not join a running release; finished() flips only after the worker
    // exits, and the GUI keeps processing events while the latch is held.
    learnOnce(preview);
    gate.reset();
    preview.beginShutdown();
    require(gate.waitStarted(), "shutdown did not request the final release");
    int heartbeats = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start(10);
    QTest::qWait(120);
    require(!preview.finished(), "shutdown reported completion while release was blocked");
    require(heartbeats > 0, "GUI stopped processing events during shutdown");
    gate.allow();
    require(QTest::qWaitFor([&] { return preview.finished(); }, 5000), "worker did not finish after release");
    preview.finalizeShutdown();
    heartbeat.stop();

    // T42-03: Verify Assist error classification contract:
    // 1. Execute phase ContractError -> ErrorCode::DataInvalid (-13)
    {
        ToolDefinition execDef = helperDefinition();
        execDef.id = "test.assist.exec_error";
        registry->registerCpp(execDef, [](const ToolServices &) { return std::make_unique<ExecuteContractErrorTool>(); });
        registry->addDefinition(execDef);

        AssistPreview execPreview;
        execPreview.configure(registry, execDef, {});
        QSignalSpy outcomeSpy(&execPreview, &AssistPreview::outcomeReady);

        AssistRequest req;
        req.learn = true;
        require(execPreview.request(req), "request was rejected");
        require(QTest::qWaitFor([&] { return !outcomeSpy.isEmpty(); }, 2000), "outcome was not received");
        const auto outcome = outcomeSpy.takeFirst().at(0).value<AssistOutcome>();
        require(outcome.status.code == static_cast<int>(ErrorCode::DataInvalid),
                "Execute phase ContractError must map to ErrorCode::DataInvalid (-13)");
        execPreview.beginShutdown();
        execPreview.finalizeShutdown();
    }

    // 2. Setup phase ContractError -> ErrorCode::InvalidArgument (-2)
    {
        ToolDefinition setupDef = helperDefinition();
        setupDef.id = "test.assist.setup_error";
        setupDef.inputs = {{"count", "数量", {Type::Int}}};
        registry->registerCpp(setupDef, [](const ToolServices &) { return std::make_unique<LatchedReleaseTool>(); });
        registry->addDefinition(setupDef);

        AssistPreview setupPreview;
        setupPreview.configure(registry, setupDef, {});
        QSignalSpy outcomeSpy(&setupPreview, &AssistPreview::outcomeReady);

        AssistRequest req;
        req.learn = true;
        req.inputs.emplace("count", std::string("not-a-number"));
        require(setupPreview.request(req), "request was rejected");
        require(QTest::qWaitFor([&] { return !outcomeSpy.isEmpty(); }, 2000), "outcome was not received");
        const auto outcome = outcomeSpy.takeFirst().at(0).value<AssistOutcome>();
        require(outcome.status.code == static_cast<int>(ErrorCode::InvalidArgument),
                "Setup phase ContractError must map to ErrorCode::InvalidArgument (-2)");
        setupPreview.beginShutdown();
        setupPreview.finalizeShutdown();
    }

    // 3. Unknown std::exception -> ErrorCode::Internal (-1)
    {
        ToolDefinition throwDef = helperDefinition();
        throwDef.id = "test.assist.throw_error";
        registry->registerCpp(throwDef, [](const ToolServices &) { return std::make_unique<UnexpectedThrowingTool>(); });
        registry->addDefinition(throwDef);

        AssistPreview throwPreview;
        throwPreview.configure(registry, throwDef, {});
        QSignalSpy outcomeSpy(&throwPreview, &AssistPreview::outcomeReady);

        AssistRequest req;
        req.learn = true;
        require(throwPreview.request(req), "request was rejected");
        require(QTest::qWaitFor([&] { return !outcomeSpy.isEmpty(); }, 2000), "outcome was not received");
        const auto outcome = outcomeSpy.takeFirst().at(0).value<AssistOutcome>();
        require(outcome.status.code == static_cast<int>(ErrorCode::Internal),
                "Unknown std::exception must map to ErrorCode::Internal (-1)");
        throwPreview.beginShutdown();
        throwPreview.finalizeShutdown();
    }

    std::cout << "ASSIST_RELEASE_PASS releases=" << gate.releases.load() << " heartbeats=" << heartbeats
              << " error_branches=3/3" << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "ASSIST_RELEASE_FAIL: " << error.what() << std::endl;
    return 1;
}
