/*
 * Copyright (c) 2020-2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ability_info_utils.h"

#include "utils.h"

namespace OHOS {
void AbilityInfoUtils::CopyAbilityInfo(AbilityInfo *des, AbilityInfo src)
{
    if (des == nullptr) {
        return;
    }

    SetAbilityInfoBundleName(des, src.bundleName);
#ifdef OHOS_APPEXECFWK_BMS_BUNDLEMANAGER
    des->isVisible = src.isVisible;
    des->abilityType = src.abilityType;
    des->launchMode = src.launchMode;
    SetAbilityInfoModuleName(des, src.moduleName);
    SetAbilityInfoName(des, src.name);
    SetAbilityInfoDescription(des, src.description);
    SetAbilityInfoIconPath(des, src.iconPath);
    SetAbilityInfoDeviceId(des, src.deviceId);
    SetAbilityInfoLabel(des, src.label);
#else
    SetAbilityInfoSrcPath(des, src.srcPath);
    SetAbilityInfoMetaData(des, src.metaData, METADATA_SIZE);
    SetAbilityInfoSkill(des, src.skills);
#endif
}

#ifdef _MINI_BMS_PARSE_METADATA_
void AbilityInfoUtils::CopyBundleProfileToAbilityInfo(AbilityInfo *des, const BundleProfile &src)
{
    if (des == nullptr) {
        return;
    }
    SetAbilityInfoMetaData(des, src.abilityInfos->metaData, METADATA_SIZE);
    SetAbilityInfoSkill(des, src.abilityInfos->skills);
}
#endif

bool AbilityInfoUtils::SetAbilityInfoBundleName(AbilityInfo *abilityInfo, const char *bundleName)
{
    if (abilityInfo == nullptr || bundleName == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->bundleName);
    abilityInfo->bundleName = Utils::Strdup(bundleName);
    return abilityInfo->bundleName != nullptr;
}

#ifdef OHOS_APPEXECFWK_BMS_BUNDLEMANAGER
bool AbilityInfoUtils::SetAbilityInfoModuleName(AbilityInfo *abilityInfo, const char *moduleName)
{
    if (abilityInfo == nullptr || moduleName == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->moduleName);
    abilityInfo->moduleName = Utils::Strdup(moduleName);
    return abilityInfo->moduleName != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoName(AbilityInfo *abilityInfo, const char *name)
{
    if (abilityInfo == nullptr || name == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->name);
    abilityInfo->name = Utils::Strdup(name);
    return abilityInfo->name != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoDescription(AbilityInfo *abilityInfo, const char *description)
{
    if (abilityInfo == nullptr || description == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->description);
    abilityInfo->description = Utils::Strdup(description);
    return abilityInfo->description != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoIconPath(AbilityInfo *abilityInfo, const char *iconPath)
{
    if (abilityInfo == nullptr || iconPath == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->iconPath);
    abilityInfo->iconPath = Utils::Strdup(iconPath);
    return abilityInfo->iconPath != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoDeviceId(AbilityInfo *abilityInfo, const char *deviceId)
{
    if (abilityInfo == nullptr || deviceId == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->deviceId);
    abilityInfo->deviceId = Utils::Strdup(deviceId);
    return abilityInfo->deviceId != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoLabel(AbilityInfo *abilityInfo, const char *label)
{
    if (abilityInfo == nullptr || label == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->label);
    abilityInfo->label = Utils::Strdup(label);
    return abilityInfo->label != nullptr;
}
#else
bool AbilityInfoUtils::SetAbilityInfoSrcPath(AbilityInfo *abilityInfo, const char *srcPath)
{
    if (abilityInfo == nullptr || srcPath == nullptr) {
        return false;
    }

    AdapterFree(abilityInfo->srcPath);
    abilityInfo->srcPath = Utils::Strdup(srcPath);
    return abilityInfo->srcPath != nullptr;
}

bool AbilityInfoUtils::SetAbilityInfoMetaData(AbilityInfo *abilityInfo, MetaData **metaData, uint32_t numOfMetaData)
{
    if (abilityInfo == nullptr || metaData == nullptr || numOfMetaData > METADATA_SIZE) {
        return false;
    }
    ClearAbilityInfoMetaData(abilityInfo->metaData, METADATA_SIZE);
    for (uint32_t i = 0; i < numOfMetaData; i++) {
        if (metaData[i] != nullptr) {
            abilityInfo->metaData[i] = reinterpret_cast<MetaData *>(AdapterMalloc(sizeof(MetaData)));
            if (abilityInfo->metaData[i] == nullptr ||
                memset_s(abilityInfo->metaData[i], sizeof(MetaData), 0, sizeof(MetaData)) != EOK) {
                ClearAbilityInfoMetaData(abilityInfo->metaData, i);
                return false;
            }
            if (metaData[i]->name != nullptr) {
                abilityInfo->metaData[i]->name = Utils::Strdup(metaData[i]->name);
            }
            if (metaData[i]->value != nullptr) {
                abilityInfo->metaData[i]->value = Utils::Strdup(metaData[i]->value);
            }
            if (metaData[i]->extra != nullptr) {
                abilityInfo->metaData[i]->extra = Utils::Strdup(metaData[i]->extra);
            }
            if ((metaData[i]->name != nullptr && abilityInfo->metaData[i]->name == nullptr) ||
                (metaData[i]->value != nullptr && abilityInfo->metaData[i]->value == nullptr) ||
                (metaData[i]->extra != nullptr && abilityInfo->metaData[i]->extra == nullptr)) {
                ClearAbilityInfoMetaData(abilityInfo->metaData, i);
                return false;
            }
        }
    }
    return true;
}

void AbilityInfoUtils::ClearStringArray(char *array[], int count)
{
    for (int i = 0; i < count; i++) {
        if (array[i] == nullptr) {
            continue;
        }
        AdapterFree(array[i]);
    }
}

void AbilityInfoUtils::CopyStringArray(char *dst[], char *const src[], int count)
{
    for (int i = 0; i < count; i++) {
        dst[i] = Utils::Strdup(src[i]);
    }
}

bool AbilityInfoUtils::SetAbilityInfoSkill(AbilityInfo *abilityInfo, Skill * const skills[])
{
    if (abilityInfo == nullptr || skills == nullptr) {
        return false;
    }
    for (int i = 0; i < SKILL_SIZE; i++) {
        if (abilityInfo->skills[i] != nullptr) {
            ClearStringArray(abilityInfo->skills[i]->entities, MAX_SKILL_ITEM);
            ClearStringArray(abilityInfo->skills[i]->actions, MAX_SKILL_ITEM);
            AdapterFree(abilityInfo->skills[i]);
        }
        if (skills[i] == nullptr) {
            return false;
        }
        abilityInfo->skills[i] = static_cast<Skill *>(AdapterMalloc(sizeof(Skill)));
        CopyStringArray(abilityInfo->skills[i]->entities, skills[i]->entities, MAX_SKILL_ITEM);
        CopyStringArray(abilityInfo->skills[i]->actions, skills[i]->actions, MAX_SKILL_ITEM);
    }
    return true;
}

void AbilityInfoUtils::ClearExtendedInfo(AbilityInfo *abilityInfo)
{
    for (int i = 0; i < METADATA_SIZE; i++) {
        if (abilityInfo->metaData[i] == nullptr) {
            continue;
        }
        AdapterFree(abilityInfo->metaData[i]->name);
        AdapterFree(abilityInfo->metaData[i]->value);
        AdapterFree(abilityInfo->metaData[i]->extra);
        AdapterFree(abilityInfo->metaData[i]);
    }
    for (int i = 0; i < SKILL_SIZE; i++) {
        if (abilityInfo->skills[i] == nullptr) {
            continue;
        }
        ClearStringArray(abilityInfo->skills[i]->entities, MAX_SKILL_ITEM);
        ClearStringArray(abilityInfo->skills[i]->actions, MAX_SKILL_ITEM);
        AdapterFree(abilityInfo->skills[i]);
    }
}
void AbilityInfoUtils::ClearAbilityInfoMetaData(MetaData **metaData, uint32_t numOfMetaData)
{
    if (metaData == nullptr || numOfMetaData > METADATA_SIZE) {
        return;
    }
    for (uint32_t i = 0; i < numOfMetaData; i++) {
        if (metaData[i] != nullptr) {
            AdapterFree(metaData[i]->name);
            AdapterFree(metaData[i]->value);
            AdapterFree(metaData[i]->extra);
            AdapterFree(metaData[i]);
        }
    }
}
#endif
} // OHOS