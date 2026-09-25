#include "infer/InferLoader.h"
#include "core/ToolRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <mutex>

namespace visionrt {
namespace {
struct Module {
    std::mutex mutex;
    QLibrary library;
    infer::CreateTool create = nullptr;
    infer::DestroyTool destroy = nullptr;
    infer::QueryDevices query = nullptr;
    QString path;

    ToolStatus load() {
        std::lock_guard lock(mutex);
        if (create && destroy && query) return {};
        if (!QCoreApplication::instance()) return {-3, "VisionRTInfer loader requires the application installation directory"};
        path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("VisionRTInfer.dll");
        if (!library.isLoaded()) {
            library.setFileName(path);
            library.setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
            if (!library.load()) return {-3, "VisionRTInfer load " + path.toUtf8().toStdString() +
                                            ": " + library.errorString().toUtf8().toStdString()};
        }
        const auto createEntry = reinterpret_cast<infer::CreateTool>(library.resolve("visionrtInferCreate"));
        const auto destroyEntry = reinterpret_cast<infer::DestroyTool>(library.resolve("visionrtInferDestroy"));
        const auto queryEntry = reinterpret_cast<infer::QueryDevices>(library.resolve("visionrtInferQueryDevices"));
        if (!createEntry || !destroyEntry || !queryEntry)
            return {-3, "VisionRTInfer export protocol unavailable: " + library.errorString().toUtf8().toStdString()};
        create = createEntry; destroy = destroyEntry; query = queryEntry;
        return {};
    }
};
Module& module() {
    // Qt's PreventUnloadHint pins the module until process termination, even
    // after this object's destructor. Native hot unloading is never attempted.
    static Module instance;
    return instance;
}

class InferAdapter final : public IToolInstance {
public:
    explicit InferAdapter(const ToolServices& services) : services_(services) {}
    ~InferAdapter() override {
        if (tool_) module().destroy(tool_);
    }
    ToolStatus initialize(const Values& parameters) override {
        const auto cleanup = release();
        if (!cleanup) return cleanup;
        auto& loaded = module();
        auto status = loaded.load();
        if (!status) return status;
        tool_ = loaded.create(&services_, &status);
        if (!tool_) return status ? ToolStatus{-3, "VisionRTInfer factory returned null"} : status;
        status = tool_->initialize(parameters);
        if (!status) {
            loaded.destroy(tool_);
            tool_ = nullptr;
        }
        return status;
    }
    ToolResult execute(const Values& inputs) override {
        if (!tool_) return infer::missingResult({-3, "VisionRTInfer implementation is not initialized"});
        return tool_->execute(inputs);
    }
    ToolStatus release() override {
        if (!tool_) return {};
        const auto status = tool_->release();
        module().destroy(tool_);
        tool_ = nullptr;
        return status;
    }
private:
    ToolServices services_;
    IToolInstance* tool_ = nullptr;
};

PortDefinition port(const char* id, TypeSpec type) {
    PortDefinition value;
    value.id = id;
    value.type = type;
    return value;
}
ToolDefinition contract() {
    ToolDefinition definition;
    definition.id = definition.registryName = "visionrt.dl.yolo";
    definition.kind = ToolKind::CppClass;
    for (const auto* id : {"model_name", "load_mode", "model_path", "engine_output_path", "labels_path", "precision"})
        definition.initParams.push_back(port(id, {Type::String}));
    for (const auto* id : {"gpu_index", "input_width", "input_height", "num_classes"})
        definition.initParams.push_back(port(id, {Type::Int}));
    definition.inputs = {port("images", {Type::ImageBatch}), port("conf_threshold", {Type::Float}),
                        port("nms_threshold", {Type::Float}), port("max_detections", {Type::Int})};
    definition.outputs = {port("images", {Type::ImageBatch}), port("rois", {Type::RoiBatch}),
        port("scores", {Type::NumericArray, DType::Float32, 2, Layout::BatchRagged}),
        port("classes", {Type::NumericArray, DType::Int64, 2, Layout::BatchRagged}),
        port("class_names", {Type::StringArray, std::nullopt, 2, Layout::BatchRagged})};
    return definition;
}
}

void registerInferTools(ToolRegistry& registry) {
    registry.registerCpp("visionrt.dl.yolo", contract(), [](const ToolServices& services) {
        return std::make_unique<InferAdapter>(services);
    });
}

GpuQueryResult queryGpuDevices() {
    auto& loaded = module();
    GpuQueryResult result;
    result.status = loaded.load();
    {
        std::lock_guard lock(loaded.mutex);
        result.modulePath = loaded.path.toUtf8().toStdString();
    }
    if (result.status) loaded.query(&result);
    return result;
}
} // namespace visionrt
