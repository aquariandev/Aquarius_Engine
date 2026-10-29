#include "SceneRecovery.hpp"
#include "ModelAssets.hpp"

#include <algorithm>
#include <utility>

std::vector<UnresolvedSceneActor> RestoreSceneActorRecords(
    const std::vector<EditorSceneModel>& records,
    const RestoreSceneActorFunction& restore) {
    std::vector<UnresolvedSceneActor> unresolved;
    for (std::size_t order = 0; order < records.size(); ++order) {
        std::string error;
        if (!restore(records[order], order, error)) {
            if (error.empty()) {
                error = "Actor restoration failed without error details.";
            }
            unresolved.push_back({order, records[order], std::move(error)});
        }
    }
    return unresolved;
}

bool TryRestoreUnresolvedSceneActor(
    UnresolvedSceneActor& unresolved, const EditorSceneModel& candidate,
    const std::function<bool(const EditorSceneModel&, std::string&)>& restore) {
    std::string error;
    if (!restore(candidate, error)) {
        if (error.empty()) {
            error = "Actor restoration failed without error details.";
        }
        unresolved.error = std::move(error);
        return false;
    }
    unresolved.source = candidate;
    unresolved.error.clear();
    return true;
}

bool TryReloadUnresolvedMaterialDescriptor(
    EditorSceneModel& candidate, ModelMaterial& materialForRestore,
    std::string& error) {
    error.clear();
    materialForRestore = candidate.material;
    if (!candidate.materialDescriptorLoadFailed ||
        !candidate.material.asset.empty() ||
        candidate.originalSerializedMaterialAsset.empty()) {
        return false;
    }

    ModelMaterial descriptorMaterial;
    if (!LoadAqenMaterial(candidate.originalSerializedMaterialAsset,
                          descriptorMaterial, error)) {
        materialForRestore.asset.clear();
        return false;
    }

    candidate.material = descriptorMaterial;
    candidate.materialDescriptorLoadFailed = false;
    materialForRestore = std::move(descriptorMaterial);
    return true;
}

EditorSceneModel PrepareSceneActorForSave(EditorSceneModel source) {
    if (source.material.asset.empty() &&
        source.materialDescriptorLoadFailed &&
        !source.originalSerializedMaterialAsset.empty()) {
        source.material.asset = source.originalSerializedMaterialAsset;
    }
    source.originalSerializedMaterialAsset.clear();
    source.materialDescriptorLoadFailed = false;
    return source;
}

bool RemoveUnresolvedSceneActor(
    std::vector<UnresolvedSceneActor>& unresolved, std::size_t sceneOrder) {
    const auto found = std::find_if(
        unresolved.begin(), unresolved.end(),
        [sceneOrder](const UnresolvedSceneActor& actor) {
            return actor.sceneOrder == sceneOrder;
        });
    if (found == unresolved.end()) {
        return false;
    }
    unresolved.erase(found);
    return true;
}

bool ApplyUnresolvedRemovalAction(
    std::vector<UnresolvedSceneActor>& unresolved,
    const UnresolvedActorAction& action) {
    if (action.type != UnresolvedActorActionType::Remove) {
        return false;
    }
    return RemoveUnresolvedSceneActor(unresolved, action.sceneOrder);
}

std::vector<EditorSceneModel> MergeOrderedSceneActors(
    std::vector<OrderedSceneActor> loaded,
    const std::vector<UnresolvedSceneActor>& unresolved) {
    loaded.reserve(loaded.size() + unresolved.size());
    for (const UnresolvedSceneActor& actor : unresolved) {
        loaded.push_back(
            {actor.sceneOrder, PrepareSceneActorForSave(actor.source)});
    }
    for (OrderedSceneActor& actor : loaded) {
        actor.source = PrepareSceneActorForSave(std::move(actor.source));
    }
    std::stable_sort(
        loaded.begin(), loaded.end(),
        [](const OrderedSceneActor& left, const OrderedSceneActor& right) {
            return left.sceneOrder < right.sceneOrder;
        });

    std::vector<EditorSceneModel> records;
    records.reserve(loaded.size());
    for (OrderedSceneActor& actor : loaded) {
        records.push_back(std::move(actor.source));
    }
    return records;
}

bool SaveEditorSceneWithRecovery(
    const std::filesystem::path& projectRoot, bool sceneWasParsed,
    const glm::vec3& cameraPosition, float cameraYaw, float cameraPitch,
    std::vector<OrderedSceneActor> loaded,
    const std::vector<UnresolvedSceneActor>& unresolved, std::string& error) {
    if (!sceneWasParsed) {
        error = "The scene was not fully restored. Fix the reported load "
                "issues before saving to avoid overwriting saved actors.";
        return false;
    }

    EditorScene scene;
    scene.cameraPosition = cameraPosition;
    scene.cameraYaw = cameraYaw;
    scene.cameraPitch = cameraPitch;
    scene.models =
        MergeOrderedSceneActors(std::move(loaded), unresolved);
    return SaveEditorScene(projectRoot, scene, error);
}
