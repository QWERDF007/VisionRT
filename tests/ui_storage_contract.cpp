#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ProcessMemory.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>
#include <iostream>
#include <algorithm>
#include <vector>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QTemporaryDir   directory;
    require(directory.isValid(), "storage fixture directory unavailable");
    const auto root  = directory.path() + "/tools-package/";
    const auto write = [&](const QString &path, const QByteArray &content)
    {
        require(QDir().mkpath(QFileInfo(root + path).absolutePath()), "storage fixture parent unavailable");
        QFile file(root + path);
        require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(),
                "storage fixture write failed");
    };
    write("module_index.json", R"({"schema_version":1,"variant":"test","tools":[
        {"tool_id":"test.storage.source","definition":"tools/custom/storage/source.json"},
        {"tool_id":"test.storage.probe","definition":"tools/custom/storage/probe.json"}],"modules":[],"runtime_files":[]})");
    write("tools/custom/storage/source.json", R"({"schema_version":4,"tool_id":"test.storage.source",
        "display_name":"大图存储","version":"1","kind":"python_function",
        "entry":{"package":"storage","file":"tool.py","symbol":"source"},"init_params":[],"inputs":[{"id":"gate","name":"执行闸门","type":"String","default":""}],"outputs":[
        {"id":"images","name":"图像","type":"Image","display":{"color_order":"BGR"}},
        {"id":"token","name":"对象编号","type":"Int"}]})");
    write("tools/custom/storage/probe.json", R"({"schema_version":4,"tool_id":"test.storage.probe",
        "display_name":"存储计数","version":"1","kind":"python_function",
        "entry":{"package":"storage","file":"tool.py","symbol":"probe"},"init_params":[],"inputs":[],"outputs":[
        {"id":"allocated","name":"分配","type":"Int"},{"id":"released","name":"释放","type":"Int"},
        {"id":"live","name":"存活","type":"Array","dtype":"int64","rank":1}]})");
    write("python/custom/storage/tool.py", R"py(import numpy as np
import weakref
import time
from pathlib import Path
allocated = 0
released = []
references = {}
def retired(token):
    released.append(token)
def source(inputs):
    global allocated
    allocated += 1
    token = allocated
    pixels = np.full((2160, 3840, 3), 33, dtype=np.uint8)
    references[token] = weakref.ref(pixels)
    weakref.finalize(pixels, retired, token)
    gate = inputs['gate']
    if gate:
        Path(gate + '.entered').write_text(str(token))
        deadline = time.monotonic() + 10
        while not Path(gate + '.allow').exists():
            if time.monotonic() >= deadline:
                raise RuntimeError('storage gate timed out')
            time.sleep(0.001)
        Path(gate + '.returned').write_text(str(token))
    return {'__status_code': 0, '__status_message': '', 'images': [pixels], 'token': token}
def probe(inputs):
    live = [token for token, reference in references.items() if reference() is not None]
    return {'__status_code': 0, '__status_message': '', 'allocated': allocated, 'released': len(released), 'live': live}
)py");
    ImageViewport viewport;
    viewport.setSize({480, 320});
    AppController app(2, directory.path());
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "storage operation did not finish");
    };
    idle();
    app.attachViewport(&viewport);
    app.addNode("test.storage.probe", 0, 0);
    idle();
    const auto monitor = app.selectedNode();
    auto      *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "storage output model unavailable");
    const auto checkCounts = [&](std::int64_t allocated, std::int64_t released, bool keeperAlive)
    {
        app.selectNode(monitor);
        app.runNode();
        idle();
        const auto allocations = outputs->capture("allocated");
        const auto releases    = outputs->capture("released");
        const auto live        = outputs->capture("live");
        require(allocations && releases && live, "storage counters missing");
        require(std::get<std::int64_t>(allocations->value) == allocated
                    && std::get<std::int64_t>(releases->value) == released,
                "deleted image storage was retained by the workbench");
        const auto &tokens = std::get<Array>(live->value).get<NestedVector<std::int64_t, 1>>();
        require(keeperAlive ? tokens == NestedVector<std::int64_t, 1>{1} : tokens.empty(),
                "deleted image revived or retained node lost its last result");
        std::cout << "Workbench storage: allocated=" << allocated << " released=" << released
                  << " live=" << tokens.size() << '\n';
    };
    app.addNode("test.storage.source", 100, 0);
    idle();
    const auto keeper = app.selectedNode();
    app.runNode();
    idle();
    require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000), "retained 4K image was not displayed");
    checkCounts(1, 0, true);
    std::vector<std::size_t> steadyWorkingSet;
    for (std::int64_t cycle = 1; cycle <= 24; ++cycle)
    {
        app.addNode("test.storage.source", 200, 0);
        idle();
        const auto transient = app.selectedNode();
        app.runNode();
        idle();
        require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000), "transient 4K image was not displayed");
        app.deleteNodes({transient});
        idle();
        checkCounts(cycle + 1, cycle, true);
        app.selectNode(keeper);
        {
            const auto token = outputs->capture("token");
            const auto image = outputs->capture("images");
            require(token && std::get<std::int64_t>(token->value) == 1 && image,
                    "deleting another node cleared the retained publication");
            const auto &pixels = std::get<DataRef<Image>>(image->value).get()[0];
            require(
                pixels.rows == 2160 && pixels.cols == 3840 && pixels.at<cv::Vec3b>(2159, 3839) == cv::Vec3b(33, 33, 33),
                "retained publication no longer owns its 4K pixels");
        }
        require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000),
                "retained node could not be displayed again");
        const auto memory = testing::getWorkingSetMemory();
#ifdef _WIN32
        require(memory.supported, "working-set probe failed");
#endif
        if (memory.supported && cycle >= 5)
            steadyWorkingSet.push_back(memory.workingSetKB);
        std::cout << "Workbench memory: cycle=" << cycle << " rss=" << memory.report << '\n';
    }
    if (!steadyWorkingSet.empty())
    {
        const auto [minimum, maximum] = std::minmax_element(steadyWorkingSet.begin(), steadyWorkingSet.end());
        const auto range              = *maximum - *minimum;
        std::cout << "Workbench steady memory: samples=" << steadyWorkingSet.size() << " min_kib=" << *minimum
                  << " max_kib=" << *maximum << " range_kib=" << range << " limit_kib=48600\n";
        require(range <= 48600, "post-warmup working set accumulated more than two 4K display frames");
    }
    else
        std::cout << "Workbench RSS trend: not_run (unsupported platform)\n";
    app.deleteNodes({keeper});
    idle();
    checkCounts(25, 25, false);
    require(app.nodes().size() == 1 && app.nodes().first().toMap()["id"].toString() == monitor,
            "deleted nodes reappeared after asynchronous polling");
    require(!viewport.hasImage(), "deleted image reappeared in the viewport");
    std::cout << "Workbench 4K deletion: cycles=24 allocated=25 released=25 final_live=0 retained_publication=1\n";
    app.addNode("test.storage.source", 100, 0);
    idle();
    const auto lateNode = app.selectedNode();
    const auto gate     = directory.path() + "/late-execution";
    require(app.setParameter("gate", false, gate), "late execution gate was rejected");
    idle();
    const auto releaseGate = [&]
    {
        QFile allow(gate + ".allow");
        return allow.open(QIODevice::WriteOnly);
    };
    auto cleanup = qScopeGuard(releaseGate);
    app.runNode();
    require(QTest::qWaitFor([&] { return QFile::exists(gate + ".entered"); }, 5000),
            "large-image execute did not enter its controlled gate");
    app.deleteNodes({lateNode});
    require(app.runState() == "Stopping", "deleting an executing node did not request Stop");
    QTest::qWait(40);
    require(!QFile::exists(gate + ".returned") && app.nodes().size() == 2,
            "node was removed before its tool invocation could return");
    require(releaseGate(), "cannot release storage execution gate");
    idle();
    {
        QFile returned(gate + ".returned");
        require(returned.open(QIODevice::ReadOnly) && returned.readAll() == "26",
                "deleted execution did not return the expected large-image result");
    }
    checkCounts(26, 26, false);
    QTest::qWait(100);
    require(app.nodes().size() == 1 && app.nodes().first().toMap()["id"].toString() == monitor && !viewport.hasImage(),
            "late runtime or display completion resurrected a deleted image node");
    checkCounts(26, 26, false);
    std::cout << "Controlled deletion: pending=Stopping returned_token=26 final_allocated=26 final_released=26 "
                 "final_live=0\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
