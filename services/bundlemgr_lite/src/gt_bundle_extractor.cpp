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

#include "gt_bundle_extractor.h"
#include "app_verify_pub.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif
#include "adapter.h"
#include "appexecfwk_errors.h"
#include "bundle_util.h"
#include "bundlems_log.h"
#include "fcntl.h"
#include "gt_bundle_manager_service.h"
#include "gt_bundle_parser.h"
#include "gt_extractor_util.h"
#include "sys/stat.h"
#include "unistd.h"
#include "utils.h"

namespace OHOS {
uint8_t GtBundleExtractor::ExtractFileDataPos(int32_t fp, uint64_t &filePos)
{
    char *bundleName = nullptr;

    int32_t ret = lseek(fp, 0, SEEK_SET);
    if (ret < 0) {
        return ERR_APPEXECFWK_INSTALL_FAILED_INTERNAL_ERROR;
    }

    uint8_t errorCode = GtExtractorUtil::ExtractFileHeaderInfo(fp, &bundleName);
    if (errorCode != ERR_OK) {
        return errorCode;
    }

    filePos = MAGIC_NUMBER_LEN + INT_LENGTH + strlen(bundleName);
    UI_Free(bundleName);
    return ERR_OK;
}

uint8_t GtBundleExtractor::ExtractHap(const char *codePath, const char *bundleName, int32_t fp, uint32_t totalFileSize,
    uint8_t bundleStyle)
{
    char *relativeFilePath = nullptr;
    char *fileName = nullptr;
    uint64_t fileSize = 0;
    uint64_t index = 0;

    if (codePath == nullptr || bundleName == nullptr) {
        return ERR_APPEXECFWK_INSTALL_FAILED_INTERNAL_ERROR;
    }

    uint8_t errorCode = ExtractFileDataPos(fp, index);
    if (errorCode != ERR_OK) {
        return errorCode;
    }

    while (index < totalFileSize) {
        errorCode = GtExtractorUtil::ExtractFileToPath(codePath, fp, fileSize, &fileName, &relativeFilePath);
        if (errorCode != ERR_OK) {
            UI_Free(relativeFilePath);
            UI_Free(fileName);
            break;
        }
        index = index + INT_LENGTH + strlen(fileName) + INT_LENGTH + strlen(relativeFilePath) + LONG_LENGTH + fileSize;
        UI_Free(relativeFilePath);
        UI_Free(fileName);
        relativeFilePath = nullptr;
        fileName = nullptr;
    }
    return ERR_OK;
}

char *GtBundleExtractor::ExtractHapProfile(int32_t fp, uint32_t totalFileSize)
{
    char *fileName = nullptr;
    char *fileData = nullptr;
    uint64_t fileSize = 0;
    uint64_t index = 0;
    uint32_t pathLen = 0;

    uint8_t errorCode = ExtractFileDataPos(fp, index);
    if (errorCode != ERR_OK) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get file data pos failed!");
        return nullptr;
    }

    while (index < totalFileSize) {
        if (GtExtractorUtil::ExtractFileAttr(fp, &fileName, pathLen, fileSize) != ERR_OK) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get file attr failed!");
            UI_Free(fileName);
            return nullptr;
        }

        int32_t fileNameLen = strlen(fileName);
        if (pathLen == 0 && (strcmp(fileName, PROFILE_NAME) == 0)) {
            UI_Free(fileName);
            fileName = nullptr;
            fileData = reinterpret_cast<char *>(AdapterMalloc(fileSize * sizeof(char)));
            if (fileData == nullptr || read(fp, fileData, fileSize) != fileSize) {
                HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get profile data failed!");
                AdapterFree(fileData);
                return nullptr;
            }
            return fileData;
        } else {
            UI_Free(fileName);
            fileName = nullptr;
            int32_t ret = lseek(fp, fileSize, SEEK_CUR);
            if (ret < 0) {
                return nullptr;
            }
        }

        index = index + INT_LENGTH + fileNameLen + INT_LENGTH + pathLen + LONG_LENGTH + fileSize;
    }
    return nullptr;
}

bool GtBundleExtractor::ExtractResourceFile(const char *path, int32_t fp, uint32_t totalFileSize)
{
    char *fileName = nullptr;
    char *relativeFilePath = nullptr;
    uint64_t fileSize = 0;
    uint64_t index = 0;

    uint8_t errorCode = ExtractFileDataPos(fp, index);
    if (errorCode != ERR_OK) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] extract file data pos failed!");
        return false;
    }
#ifdef _MINI_BMS_PERMISSION_
    RefreshAllServiceTimeStamp();
#endif
    while (index < totalFileSize) {
        if (GtExtractorUtil::ExtractFileAttr(fp, &fileName, &relativeFilePath, fileSize) != ERR_OK) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get file attr failed!");
            UI_Free(fileName);
            UI_Free(relativeFilePath);
            break;
        }

        int32_t fileNameLen = strlen(fileName);
        if ((strlen(relativeFilePath) == 0 && (strcmp(fileName, PROFILE_NAME) == 0)) ||
            (!BundleUtil::StartWith(relativeFilePath, ASSET_JS_PATH) &&
            !BundleUtil::StartWith(relativeFilePath, NEW_ASSET_JS_PATH))) {
            if (!GtExtractorUtil::HasWrittenFile(path, relativeFilePath, fileName, fp, fileSize)) {
                UI_Free(fileName);
                UI_Free(relativeFilePath);
                return false;
            }
        } else {
            int32_t ret = lseek(fp, fileSize, SEEK_CUR);
            if (ret < 0) {
                UI_Free(fileName);
                UI_Free(relativeFilePath);
                return false;
            }
        }

        index = index + INT_LENGTH + fileNameLen + INT_LENGTH + strlen(relativeFilePath) + LONG_LENGTH + fileSize;
        UI_Free(fileName);
        UI_Free(relativeFilePath);
        fileName = nullptr;
        relativeFilePath = nullptr;
    }
    return true;
}

uint8_t GtBundleExtractor::ExtractInstallMsg(
    const char *path, char **bundleName, char **label, char **smallIconPath, char **bigIconPath, uint8_t &actionService)
{
#ifdef _MINI_BMS_PERMISSION_
    RefreshAllServiceTimeStamp();
#endif
    if (!BundleUtil::CheckRealPath(path)) {
        return ERR_APPEXECFWK_INSTALL_FAILED_PARAM_ERROR;
    }
#ifdef _MINI_BMS_
    int32_t totalFileSize = APPVERI_GetUnsignedFileLength(path);
    if (totalFileSize == V_ERR) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get unsigned file length failed!");
        return ERR_APPEXECFWK_INSTALL_FAILED_INTERNAL_ERROR;
    }
#else
    int32_t totalFileSize = BundleUtil::GetFileSize(path);
#endif

    int32_t fp = open(path, O_RDONLY, S_IREAD);
    if (fp < 0) {
        return ERR_APPEXECFWK_INSTALL_FAILED_FILE_NOT_EXISTS;
    }
    // extractor config.json、 resources dir and resources.index in TMP_RESOURCE_DIR
    if (!ExtractResourceFile(TMP_RESOURCE_DIR, fp, static_cast<uint32_t>(totalFileSize))) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] extract resource file failed!");
        close(fp);
        return ERR_APPEXECFWK_INSTALL_FAILED_PARSE_PROFILE_ERROR;
    }
    close(fp);
    // get bundleName、 label、 smallIconPath and bigIconPath from bundleInfo
    BundleRes bundleRes = { 0 };
    BundleInfo *bundleInfo = GtBundleParser::ParseHapProfile(TMP_RESOURCE_DIR, &bundleRes);
    if (bundleInfo == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] parse hap profile get bundle info failed!");
        return ERR_APPEXECFWK_INSTALL_FAILED_PARSE_PROFILE_ERROR;
    }
    if (bundleRes.abilityRes != nullptr) {
        AdapterFree(bundleRes.abilityRes);
    }
    *bundleName = Utils::Strdup(bundleInfo->bundleName);
    *label = Utils::Strdup(bundleInfo->label);
    *smallIconPath = Utils::Strdup(bundleInfo->smallIconPath);
    *bigIconPath = Utils::Strdup(bundleInfo->bigIconPath);
    // find bundleInfo->abilityInfo->skills[i]->actions whether contain HOST_APDU_SERVICE
    actionService = FindSkillService(bundleInfo->abilityInfo);
    if (*bundleName == nullptr || *label == nullptr || *smallIconPath == nullptr || *bigIconPath == nullptr) {
        BundleInfoUtils::FreeBundleInfo(bundleInfo);
        return ERR_APPEXECFWK_INSTALL_FAILED_PARSE_ABILITIES_ERROR;
    }
    BundleInfoUtils::FreeBundleInfo(bundleInfo);
    return ERR_OK;
}

uint8_t GtBundleExtractor::ParseBundleInfoGetActionService(const char *bundleName)
{
    if (bundleName == nullptr) {
        return ERR_OK;
    }
    BundleInfo *bundleInfo = OHOS::GtManagerService::GetInstance().QueryBundleInfo(bundleName);
    if (bundleInfo == nullptr) {
        return ERR_OK;
    }
    // find bundleInfo->abilityInfo->skills[i]->actions whether contain HOST_APDU_SERVICE
    uint8_t actionService = FindSkillService(bundleInfo->abilityInfo);
    return actionService;
}
uint8_t GtBundleExtractor::FindSkillService(AbilityInfo *abilityInfo)
{
    if (abilityInfo == nullptr) {
        return 0;
    }
    for (int i = 0; i < SKILL_SIZE; i++) {
        if (abilityInfo->skills[i] != nullptr) {
            if (CompareStringArray(abilityInfo->skills[i]->actions, MAX_SKILL_ITEM)) {
                return 1;
            }
        }
    }
    return 0;
}
uint8_t GtBundleExtractor::CompareStringArray(char *actions[], int count)
{
    for (int i = 0; i < count; i++) {
        if (actions[i] == nullptr) {
            continue;
        }
        if (strcmp(actions[i], SERVICE_NAME) == 0) {
            return 1;
        }
    }
    return 0;
}

uint8_t GtBundleExtractor::ExtractBundleParam(const char *path, int32_t &fpStart, char **bundleName)
{
    if (!BundleUtil::CheckRealPath(path)) {
        return ERR_APPEXECFWK_INSTALL_FAILED_PARAM_ERROR;
    }

    int32_t fp = open(path, O_RDONLY, S_IREAD);
    if (fp < 0) {
        return ERR_APPEXECFWK_INSTALL_FAILED_FILE_NOT_EXISTS;
    }
    fpStart = fp;

    uint8_t errorCode = GtExtractorUtil::ExtractFileHeaderInfo(fp, bundleName);
    if (errorCode != ERR_OK) {
        return errorCode;
    }

    if (strlen(*bundleName) > MAX_BUNDLE_NAME_LEN || strlen(*bundleName) < MIN_BUNDLE_NAME_LEN) {
        return ERR_APPEXECFWK_INSTALL_FAILED_PARSE_INVALID_BUNDLENAME_LENGTH;
    }

    if (strstr(*bundleName, "../") != nullptr) {
        return ERR_APPEXECFWK_INSTALL_FAILED_PARSE_INVALID_BUNDLENAME;
    }
    return ERR_OK;
}
} // namespace OHOS