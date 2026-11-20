#pragma once
namespace visionrt {
enum class NodeState { Idle = 0, Running = 1, Succeeded = 2, Failed = 3, Canceled = 4 };
enum class CatalogEntryKind { Folder = 0, Tool = 1 };
enum class RelationKind { Image = 0, Alignment = 1, SameGeometry = 2 };
enum class UnsavedDecision { Save = 0, Discard = 1, Cancel = 2 };
enum class SettingsPage { None = 0, Tools = 1, Runtime = 2, Logging = 3 };
}
