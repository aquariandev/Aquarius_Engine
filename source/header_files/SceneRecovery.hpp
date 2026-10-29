#pragma once

#include "EditorScene.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct UnresolvedSceneActor {
    std::size_t sceneOrder = 0;
    EditorSceneModel source;
    std::string error;
};

struct OrderedSceneActor {
    std::size_t sceneOrder = 0;
    EditorSceneModel source;
};

enum class UnresolvedActorActionType {
    Retry,
    Relink,
    Remove
};

struct UnresolvedActorAction {
    UnresolvedActorActionType type = UnresolvedActorActionType::Retry;
    std::size_t sceneOrder = 0;
    std::filesystem::path replacementPath;
};

using RestoreSceneActorFunction =
    std::function<bool(const EditorSceneModel&, std::size_t, std::string&)>;

std::vector<UnresolvedSceneActor> RestoreSceneActorRecords(
    const std::vector<EditorSceneModel>& records,
    const RestoreSceneActorFunction& restore);
bool TryRestoreUnresolvedSceneActor(
    UnresolvedSceneActor& unresolved, const EditorSceneModel& candidate,
    const std::function<bool(const EditorSceneModel&, std::string&)>& restore);
bool TryReloadUnresolvedMaterialDescriptor(
    EditorSceneModel& candidate, ModelMaterial& materialForRestore,
    std::string& error);
EditorSceneModel PrepareSceneActorForSave(EditorSceneModel source);
bool RemoveUnresolvedSceneActor(
    std::vector<UnresolvedSceneActor>& unresolved, std::size_t sceneOrder);
bool ApplyUnresolvedRemovalAction(
    std::vector<UnresolvedSceneActor>& unresolved,
    const UnresolvedActorAction& action);
std::vector<EditorSceneModel> MergeOrderedSceneActors(
    std::vector<OrderedSceneActor> loaded,
    const std::vector<UnresolvedSceneActor>& unresolved);
bool SaveEditorSceneWithRecovery(
    const std::filesystem::path& projectRoot, bool sceneWasParsed,
    const glm::vec3& cameraPosition, float cameraYaw, float cameraPitch,
    std::vector<OrderedSceneActor> loaded,
    const std::vector<UnresolvedSceneActor>& unresolved, std::string& error);
