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

#include "gt_bundle_manager_service.h"
#include "aafwk_event_error_id.h"
#include "aafwk_event_error_code.h"
#include "ability_info_utils.h"
#include "ability_message_id.h"
#include "appexecfwk_errors.h"
#include "bundle_common.h"
#include "bundle_message_id.h"
#include "bundle_util.h"
#include "bundlems_log.h"
#include "cmsis_os2.h"
#include "cstdio"
#include "dirent.h"
#include "fcntl.h"
#include "gt_bundle_extractor.h"
#include "gt_bundle_parser.h"
#include "gt_extractor_util.h"
#include "jerryscript_adapter.h"
#include "los_tick.h"
#include "sys/stat.h"
#include "unistd.h"
#include "utils.h"
#include "want.h"

using namespace OHOS::ACELite;

namespace OHOS {
const uint8_t OPERATION_DOING = 200;
const uint8_t BMS_INSTALLATION_START = 101;
const uint8_t BMS_UNINSTALLATION_START = 104;
const uint8_t BMS_INSTALLATION_COMPLETED = 100;

GtManagerService::GtManagerService()
{
    installer_ = new (std::nothrow) GtBundleInstaller();
    bundleResList_ = new (std::nothrow) List<BundleRes *>();
    bundleMap_ = BundleMap::GetInstance();
    bundleInstallMsg_ = nullptr;
    jsEngineVer_ = nullptr;
    installedThirdBundleNum_ = 0;
    preAppList_ = nullptr;
    updateFlag_ = false;
    oldVersionCode_ = -1;
    listenList_ = new (std::nothrow) List<InstallerCallback>();
}

GtManagerService::~GtManagerService()
{
    delete installer_;
    installer_ = nullptr;
    delete bundleResList_;
    bundleResList_ = nullptr;
    delete listenList_;
    listenList_ = nullptr;
}

bool GtManagerService::Install(const char *hapPath, const InstallParam *installParam,
    InstallerCallback installerCallback)
{
    if (installer_ == nullptr) {
        installer_ = new (std::nothrow) GtBundleInstaller();
    }
    if (hapPath == nullptr) {
        return false;
    }
    if (installerCallback == nullptr) {
        return false;
    }
    char *path = reinterpret_cast<char *>(AdapterMalloc(strlen(hapPath) + 1));
    if (path == nullptr) {
        return false;
    }
    if (strncpy_s(path, strlen(hapPath) + 1, hapPath, strlen(hapPath)) != EOK) {
        AdapterFree(path);
        return false;
    }

    // delete resource temp dir first
    (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
    // create new bundleInstallMsg
    bundleInstallMsg_ = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (bundleInstallMsg_ == nullptr) {
        AdapterFree(path);
        return false;
    }
    if (memset_s(bundleInstallMsg_, sizeof(BundleInstallMsg), 0, sizeof(BundleInstallMsg)) != EOK) {
        AdapterFree(bundleInstallMsg_);
        AdapterFree(path);
        return false;
    }
    // set bundleName、label、smallIconPath、bigIconPath in bundleInstallMsg_
    uint8_t ret = GtBundleExtractor::ExtractInstallMsg(path,
        &(bundleInstallMsg_->bundleName),
        &(bundleInstallMsg_->label),
        &(bundleInstallMsg_->smallIconPath),
        &(bundleInstallMsg_->bigIconPath),
        bundleInstallMsg_->actionService);
    if (ret != 0) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Install extract install msg failed, ret is %{public}d", ret);
        if (bundleInstallMsg_->actionService) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] install fail and actionService is on.");
            (void)ReportHceInstallCallback(ret, BUNDLE_INSTALL_FAIL, BMS_INSTALLATION_COMPLETED);
        }
        char *name = strrchr(path, '/');
        bundleInstallMsg_->bundleName = Utils::Strdup(name + 1);
        (void) ReportInstallCallback(ret, BUNDLE_INSTALL_FAIL, BMS_INSTALLATION_COMPLETED, installerCallback);
        ClearSystemBundleInstallMsg();
        (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
        AdapterFree(path);
        return false;
    }

    updateFlag_ = false;
    oldVersionCode_ = -1;
    BundleInfo *installedInfo = bundleMap_->Get(bundleInstallMsg_->bundleName);
    if (installedInfo != nullptr) {
        updateFlag_ = true;
        oldVersionCode_ = installedInfo->versionCode;
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] App is in the updated state");
    }

    SetCurrentBundle(bundleInstallMsg_->bundleName);
    (void) ReportInstallCallback(OPERATION_DOING, 0, BMS_INSTALLATION_START, installerCallback);
#ifdef _MINI_BMS_PERMISSION_
    DisableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    ret = installer_->Install(path, installerCallback);
#ifdef _MINI_BMS_PERMISSION_
    EnableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] Install ret is %d", ret);
    if (ret == 0) {
        (void)ReportInstallCallback(ret, BUNDLE_INSTALL_OK, BMS_INSTALLATION_COMPLETED, installerCallback);
        if (bundleInstallMsg_->actionService) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] install success and actionService is on.");
            (void)ReportHceInstallCallback(ret, BUNDLE_INSTALL_OK, BMS_INSTALLATION_COMPLETED);
        }
    } else {
        (void)ReportInstallCallback(ret, BUNDLE_INSTALL_FAIL, BMS_INSTALLATION_COMPLETED, installerCallback);
        if (bundleInstallMsg_->actionService) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] install fail and actionService is on.");
            (void)ReportHceInstallCallback(ret, BUNDLE_INSTALL_FAIL, BMS_INSTALLATION_COMPLETED);
        }
    }
    SetCurrentBundle(nullptr);
    ClearSystemBundleInstallMsg();
    (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
    AdapterFree(path);
    return true;
}

bool GtManagerService::Uninstall(const char *bundleName, const InstallParam *installParam,
    InstallerCallback installerCallback)
{
    if (installer_ == nullptr) {
        installer_ = new (std::nothrow) GtBundleInstaller();
    }
    if (bundleName == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Parsed bundleName to be uninstalled is null");
        return false;
    }
    if (installerCallback == nullptr) {
        return false;
    }
    char *innerBundleName = reinterpret_cast<char *>(AdapterMalloc(strlen(bundleName) + 1));
    if (innerBundleName == nullptr) {
        return false;
    }
    if (strncpy_s(innerBundleName, strlen(bundleName) + 1, bundleName, strlen(bundleName)) != EOK) {
        AdapterFree(innerBundleName);
        return false;
    }
    SetCurrentBundle(innerBundleName);
    // obtain the skill.action whether contain HOST_APDU_SERVICE
    uint8_t actionService = GtBundleExtractor::ParseBundleInfoGetActionService(innerBundleName);
    (void) ReportUninstallCallback(
        OPERATION_DOING, BUNDLE_UNINSTALL_DOING, innerBundleName, BMS_UNINSTALLATION_START, installerCallback);
#ifdef _MINI_BMS_PERMISSION_
    DisableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    uint8_t ret = installer_->Uninstall(innerBundleName);
#ifdef _MINI_BMS_PERMISSION_
    EnableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] Uninstall ret is %d", ret);
    if (ret == 0) {
        (void) ReportUninstallCallback(
            ret, BUNDLE_UNINSTALL_OK, innerBundleName, BMS_INSTALLATION_COMPLETED, installerCallback);
        if (actionService != 0) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] uninstall success and actionService is on.");
            (void) ReportHceUninstallCallback(ret, BUNDLE_UNINSTALL_OK, innerBundleName, BMS_INSTALLATION_COMPLETED);
        }
    } else {
        (void) ReportUninstallCallback(
            ret, BUNDLE_UNINSTALL_FAIL, innerBundleName, BMS_INSTALLATION_COMPLETED, installerCallback);
        if (actionService != 0) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] uninstall fail and actionService is on.");
            (void) ReportHceUninstallCallback(ret, BUNDLE_UNINSTALL_FAIL, innerBundleName, BMS_INSTALLATION_COMPLETED);
        }
    }

    SetCurrentBundle(nullptr);
    AdapterFree(innerBundleName);
    return true;
}

bool GtManagerService::GetInstallState(const char *bundleName, InstallState *installState, uint8_t *installProcess)
{
    if (bundleName == nullptr) {
        return false;
    }
    BundleInfo *installedInfo = bundleMap_->Get(bundleName);
    if (installedInfo != nullptr) {
        bool isUpdateSuccess = updateFlag_ && oldVersionCode_ < installedInfo->versionCode;
        if (!updateFlag_ || isUpdateSuccess) {
            *installState = BUNDLE_INSTALL_OK;
            *installProcess = BMS_INSTALLATION_COMPLETED;
            return true;
        }
    }
    if (bundleInstallMsg_ == nullptr || bundleInstallMsg_->bundleName == nullptr) {
        *installState = BUNDLE_INSTALL_FAIL;
        *installProcess = 0;
        return true;
    }
    if (strcmp(bundleName, bundleInstallMsg_->bundleName) == 0) {
        *installState = bundleInstallMsg_->installState;
        *installProcess = bundleInstallMsg_->installProcess;
        return true;
    }
    *installState = BUNDLE_INSTALL_FAIL;
    *installProcess = 0;
    return true;
}

uint32_t GtManagerService::GetBundleSize(const char *bundleName)
{
    if (bundleName == nullptr) {
        return 0;
    }
    BundleInfo *installedInfo = bundleMap_->Get(bundleName);
    if (installedInfo == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] failed to get bundle size because the bundle does not exist!");
        return 0;
    }
    char *codePath = installedInfo->codePath;
    uint32_t codeBundleSize = BundleUtil::GetFileFolderSize(codePath);
    if (codeBundleSize == 0) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] failed to get code bundle size!");
        return 0;
    }
    char *dataPath = installedInfo->dataPath;
    uint32_t dataBundleSize = BundleUtil::GetFileFolderSize(dataPath);
    return codeBundleSize + dataBundleSize;
}

uint8_t GtManagerService::QueryAbilityInfo(const Want *want, AbilityInfo *abilityInfo)
{
    if (want->element == nullptr) {
        return 0;
    }
    const char *bundleName = want->element->bundleName;
    BundleInfo *bundleInfo = OHOS::GtManagerService::GetInstance().QueryBundleInfo(bundleName);
    if (bundleInfo == nullptr) {
        return 0;
    }
    AbilityInfo *ability = bundleInfo->abilityInfo;
    if (ability == nullptr) {
        return 0;
    }
    OHOS::AbilityInfoUtils::SetAbilityInfoBundleName(abilityInfo, bundleName);
    OHOS::AbilityInfoUtils::SetAbilityInfoSrcPath(abilityInfo, ability->srcPath);
    return 1;
}

uint8_t GtManagerService::QueryAbilityInfos(const Want *want, AbilityInfo **abilityInfo, int32_t *len)
{
    if (want == nullptr || abilityInfo == nullptr || want->actions == nullptr || bundleMap_ == nullptr) {
        return 0;
    }
    int32_t abilityInfoLen = 0;
    List<BundleInfo *> *bundleInfos_ = new (std::nothrow) List<BundleInfo *>();
    bundleMap_->GetBundleInfosInner(*bundleInfos_);

    for (auto node = bundleInfos_->Begin(); node != bundleInfos_->End(); node = node->next_) {
        BundleInfo *info = node->value_;
        if (info == nullptr || info->abilityInfo == nullptr) {
            continue;
        }
        // find bundleInfo->abilityInfo->skills[i]->actions whether contain target string
        if (MatchSkills(want, info->abilityInfo->skills)) {
            abilityInfoLen++;
        }
    }
    // Request memory
    AbilityInfo *infos = reinterpret_cast<AbilityInfo *>(AdapterMalloc(sizeof(AbilityInfo) * abilityInfoLen));
    if (infos == nullptr ||
        memset_s(infos, sizeof(AbilityInfo) * abilityInfoLen, 0, sizeof(AbilityInfo) * abilityInfoLen) != EOK) {
        AdapterFree(infos);
        return ERR_APPEXECFWK_QUERY_INFOS_INIT_ERROR;
    }
    *abilityInfo = infos;
    // copy the abilityInfo from bundleInfos
    for (auto node = bundleInfos_->Begin(); node != bundleInfos_->End(); node = node->next_) {
        BundleInfo *info = node->value_;
        if (info == nullptr || info->abilityInfo == nullptr) {
            continue;
        }
        // find bundleInfo->abilityInfo->skills[i]->actions whether contain HOST_APDU_SERVICE
        if (MatchSkills(want, info->abilityInfo->skills)) {
            OHOS::AbilityInfoUtils::CopyAbilityInfo(infos++, *(info->abilityInfo));
        }
    }
    *len = abilityInfoLen;
    return 1;
}
bool GtManagerService::MatchSkills(const Want *want, Skill *const skills[])
{
    if (skills == nullptr || want == nullptr) {
        return false;
    }
    for (int32_t i = 0; i < SKILL_SIZE; i++) {
        if (skills[i] == nullptr) {
            return false;
        }
        if (isMatchActions(want->actions, skills[i]->actions) || isMatchEntities(want->entities, skills[i]->entities)) {
            return true;
        }
    }
    return false;
}
bool GtManagerService::isMatchActions(const char *actions, char *const skillActions[])
{
    if (actions == nullptr || skillActions == nullptr) {
        return false;
    }
    for (int32_t i = 0; i < MAX_SKILL_ITEM; i++) {
        if (skillActions[i] == nullptr) {
            return false;
        }
        if (isMatch(actions, skillActions[i])) {
            return true;
        }
    }
    return false;
}
bool GtManagerService::isMatchEntities(const char *entities, char *const skillEntities[])
{
    if (entities == nullptr) {
        return true;
    }
    for (int32_t i = 0; i < MAX_SKILL_ITEM; i++) {
        if (skillEntities[i] == nullptr) {
            return false;
        }
        if (isMatch(entities, skillEntities[i])) {
            return true;
        }
    }
    return false;
}
bool GtManagerService::isMatch(const char *dst, const char *src)
{
    // compare whether is ohos.nfc.cardemulation.action.HOST_APDU_SERVICE
    return strcmp(dst, src) == 0;
}

uint8_t GtManagerService::GetBundleInfo(const char *bundleName, int32_t flags, BundleInfo &bundleInfo)
{
    if (bundleMap_ == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    return bundleMap_->GetBundleInfo(bundleName, flags, bundleInfo);
}

uint8_t GtManagerService::GetBundleInfos(const int flags, BundleInfo **bundleInfos, int32_t *len)
{
    if (bundleMap_ == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    return bundleMap_->GetBundleInfos(flags, bundleInfos, len);
}

uint8_t GtManagerService::GetBundleInfosNoReplication(const int flags, BundleInfo **bundleInfos, int32_t *len)
{
    if (bundleMap_ == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    return bundleMap_->GetBundleInfosNoReplication(flags, bundleInfos, len);
}

bool GtManagerService::RegisterInstallerCallback(InstallerCallback installerCallback)
{
    if (installerCallback == nullptr) {
        return false;
    }
#ifdef BC_TRANS_ENABLE
    ScanPackages();
#endif
    InstallPreBundle(systemPathList_, installerCallback);
    return true;
}

void GtManagerService::InstallPreBundle(List<ToBeInstalledApp *> systemPathList, InstallerCallback installerCallback)
{
    if (!BundleUtil::IsDir(JSON_PATH_NO_SLASH_END)) {
        BundleUtil::MkDirs(JSON_PATH_NO_SLASH_END);
        InstallAllSystemBundle(installerCallback);
        RemoveSystemAppPathList(&systemPathList);
        return;
    }
    // get third system bundle uninstall record
    cJSON *uninstallRecord = BundleUtil::GetJsonStream(UNINSTALL_THIRD_SYSTEM_BUNDLE_JSON);
    if (uninstallRecord == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] InstallPreBundle uninstallRecord is nullptr!");
        (void) unlink(UNINSTALL_THIRD_SYSTEM_BUNDLE_JSON);
    }

    // scan system apps and third system apps
#ifdef _MINI_BMS_PERMISSION_
    DisableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    ScanSystemApp(uninstallRecord, &systemPathList_);
    if (uninstallRecord != nullptr) {
        cJSON_Delete(uninstallRecord);
    }

    // scan third apps
    ScanThirdApp(INSTALL_PATH, &systemPathList_);
#ifdef _MINI_BMS_PERMISSION_
    EnableServiceWdg();
    RefreshAllServiceTimeStamp();
#endif
    for (auto node = systemPathList.Begin(); node != systemPathList.End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        if (!BundleUtil::IsFile(toBeInstalledApp->path) ||
            !BundleUtil::EndWith(toBeInstalledApp->path, INSTALL_FILE_SUFFIX)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Pre install file path is invalid");
            continue;
        }
        if (toBeInstalledApp->isUpdated) {
            (void) ReloadBundleInfo(toBeInstalledApp->installedPath, toBeInstalledApp->appId,
                toBeInstalledApp->isSystemApp);
        }
        (void) Install(toBeInstalledApp->path, nullptr, installerCallback);
        RefreshAllServiceTimeStamp();
    }
    RemoveSystemAppPathList(&systemPathList);
}

void GtManagerService::InstallAllSystemBundle(InstallerCallback installerCallback)
{
    PreAppList *list = preAppList_;
    if (list == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] InstallAllSystemBundle InitAllAppInfo fail, list is nullptr");
        return;
    }

    PreAppList *currentNode = nullptr;
    PreAppList *nextNode = nullptr;
    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(currentNode, nextNode, &list->appDoubleList, PreAppList, appDoubleList) {
        if (currentNode == nullptr) {
            return;
        }
        if ((strcmp(((PreAppList *)currentNode)->filePath, ".") == 0) ||
            (strcmp(((PreAppList *)currentNode)->filePath, "..") == 0)) {
            continue;
        }

        if (!BundleUtil::IsFile(((PreAppList *)currentNode)->filePath) ||
            !BundleUtil::EndWith(((PreAppList *)currentNode)->filePath, INSTALL_FILE_SUFFIX)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Install all system bundle file path is invalid");
            return;
        }
        (void) Install(((PreAppList *)currentNode)->filePath, nullptr, installerCallback);
    }
    GtManagerService::FreePreAppInfo(list);
}

void GtManagerService::ClearSystemBundleInstallMsg()
{
    if (bundleInstallMsg_ == nullptr) {
        return;
    }

    if (bundleInstallMsg_->bundleName != nullptr) {
        AdapterFree(bundleInstallMsg_->bundleName);
        bundleInstallMsg_->bundleName = nullptr;
    }

    if (bundleInstallMsg_->label != nullptr) {
        AdapterFree(bundleInstallMsg_->label);
        bundleInstallMsg_->label = nullptr;
    }

    if (bundleInstallMsg_->smallIconPath != nullptr) {
        AdapterFree(bundleInstallMsg_->smallIconPath);
        bundleInstallMsg_->smallIconPath = nullptr;
    }

    if (bundleInstallMsg_->bigIconPath != nullptr) {
        AdapterFree(bundleInstallMsg_->bigIconPath);
        bundleInstallMsg_->bigIconPath = nullptr;
    }

    AdapterFree(bundleInstallMsg_);
    bundleInstallMsg_ = nullptr;
}

void GtManagerService::ScanPackages()
{
#ifdef BC_TRANS_ENABLE
    JerryBmsPsRamMemInit();
    bms_task_context_init();
    jsEngineVer_ = get_jerry_version_no();
    if (jsEngineVer_ == nullptr) {
        HILOG_WARN(HILOG_MODULE_AAFWK, "[BMS] get jsEngine version fail when restart!");
    }
#endif
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] get jsEngine version success!");
}

void GtManagerService::RemoveSystemAppPathList(List<ToBeInstalledApp *> *systemPathList)
{
    if (systemPathList == nullptr) {
        return;
    }

    for (auto node = systemPathList->Begin(); node != systemPathList->End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        if (toBeInstalledApp != nullptr) {
            AdapterFree(toBeInstalledApp->installedPath);
            AdapterFree(toBeInstalledApp->path);
            AdapterFree(toBeInstalledApp->appId);
            UI_Free(toBeInstalledApp);
        }
    }
}

void GtManagerService::ScanSystemApp(const cJSON *uninstallRecord, List<ToBeInstalledApp *> *systemPathList)
{
    PreAppList *list = preAppList_;
    if (list == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] ScanSystemApp InitAllAppInfo fail, list is nullptr");
        return;
    }

    uint8_t scanFlag = 0;
    char *bundleName = nullptr;
    int32_t versionCode = -1;
    PreAppList *currentNode = nullptr;
    PreAppList *nextNode = nullptr;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(currentNode, nextNode, &list->appDoubleList, PreAppList, appDoubleList) {
        if (currentNode == nullptr) {
            return;
        }
        if ((strcmp(((PreAppList *)currentNode)->filePath, ".") == 0) ||
            (strcmp(((PreAppList *)currentNode)->filePath, "..") == 0)) {
            continue;
        }

        if (BundleUtil::StartWith(((PreAppList *)currentNode)->filePath, SYSTEM_BUNDLE_PATH)) {
            scanFlag = SYSTEM_APP_FLAG;
        } else if (BundleUtil::StartWith(((PreAppList *)currentNode)->filePath, THIRD_SYSTEM_BUNDLE_PATH)) {
            scanFlag = THIRD_SYSTEM_APP_FLAG;
        } else {
            continue; // skip third app
        }

        // scan system app
        bool res = CheckSystemBundleIsValid(((PreAppList *)currentNode)->filePath, &bundleName, versionCode);
        if (!res) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] ScanSystemApp CheckSystemBundleIsValid failed!");
            APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_INVALID_SYSTEM_APP);
            AdapterFree(bundleName);
            continue;
        }

        // third system app cannot restore after uninstall
        if (scanFlag == THIRD_SYSTEM_APP_FLAG &&
            CheckThirdSystemBundleHasUninstalled(bundleName, uninstallRecord)) {
            AdapterFree(bundleName);
            continue;
        }

        ReloadEntireBundleInfo(((PreAppList *)currentNode)->filePath, bundleName,
            systemPathList, versionCode, scanFlag);
        AdapterFree(bundleName);
    }
    GtManagerService::FreePreAppInfo(list);
}

void GtManagerService::ScanThirdApp(const char *appDir, const List<ToBeInstalledApp *> *systemPathList)
{
    dirent *ent = nullptr;

    if (appDir == nullptr) {
        return;
    }

    DIR *dir = opendir(appDir);
    if (dir == nullptr) {
        return;
    }
    char *bundleName = reinterpret_cast<char *>(AdapterMalloc(MAX_BUNDLE_NAME_LEN + 1));
    if (bundleName == nullptr) {
        closedir(dir);
        return;
    }
    int32_t entLen = 0;
    while ((ent = readdir(dir)) != nullptr) {
        ++entLen;
        if (memset_s(bundleName, MAX_BUNDLE_NAME_LEN + 1, 0, MAX_BUNDLE_NAME_LEN + 1) != EOK) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] memset fail when initialize bundle name!");
            break;
        }

        if (strcpy_s(bundleName, MAX_BUNDLE_NAME_LEN + 1, ent->d_name) != 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] failed to copy bundle name!");
            break;
        }

        if ((strcmp(bundleName, ".") == 0) || (strcmp(bundleName, "..")) == 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] strcmp fail when reload third app!");
            continue;
        }

        int32_t len = strlen(appDir) + 1 + strlen(bundleName) + 1;
        char *appPath = reinterpret_cast<char *>(UI_Malloc(len));
        if (appPath == nullptr) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] malloc fail when reload third app!");
            break;
        }

        if (sprintf_s(appPath, len, "%s/%s", appDir, bundleName) < 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] strcat fail when reload third app!");
            UI_Free(appPath);
            break;
        }

        if (!BundleUtil::IsDir(appPath)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] app path is not dir when reload third app!");
            UI_Free(appPath);
            continue;
        }

        if (IsSystemBundleInstalledPath(appPath, systemPathList)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] app path is not third bundle path!");
            UI_Free(appPath);
            continue;
        }

        if (installedThirdBundleNum_ >= MAX_THIRD_BUNDLE_NUMBER) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] third bundle reload number is %d!",
                installedThirdBundleNum_);
            UI_Free(appPath);
            continue;
        }

        ReloadEntireBundleInfo(appPath, bundleName, nullptr, -1, THIRD_APP_FLAG);
        UI_Free(appPath);
    }

    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] third app number is %{public}d", entLen);
    AdapterFree(bundleName);
    closedir(dir);
}

bool GtManagerService::IsSystemBundleInstalledPath(const char *appPath, const List<ToBeInstalledApp *> *systemPathList)
{
    if (appPath == nullptr || systemPathList == nullptr) {
        return false;
    }

    for (auto node = systemPathList->Begin(); node != systemPathList->End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        if (toBeInstalledApp->installedPath != nullptr &&
            strcmp(appPath, toBeInstalledApp->installedPath) == 0) {
            return true;
        }
    }
    return false;
}

bool GtManagerService::CheckSystemBundleIsValid(const char *appPath, char **bundleName, int32_t &versionCode)
{
    if (appPath == nullptr || bundleName == nullptr) {
        return false;
    }

    if (!BundleUtil::EndWith(appPath, INSTALL_FILE_SUFFIX)) {
        return false;
    }

    if (!GtBundleParser::ParseBundleAttr(appPath, bundleName, versionCode)) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] parse bundle attr fail!");
        return false;
    }

    if (*bundleName != nullptr && strlen(*bundleName) >= MAX_BUNDLE_NAME_LEN) {
        return false;
    }
    return true;
}

void GtManagerService::ReloadEntireBundleInfo(const char *appPath, const char *bundleName,
    List<ToBeInstalledApp *> *systemPathList, int32_t versionCode, uint8_t scanFlag)
{
    char *codePath = nullptr;
    char *appId = nullptr;
    int32_t oldVersionCode = -1;

    if (appPath == nullptr || bundleName == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] ReloadEntireBundleInfo app path or bundle name is nullptr!");
        APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_UNKNOWN_BUNDLE_INFO);
        return;
    }

    if (QueryBundleInfo(bundleName) != nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] bundle has been reloaded!");
        return;
    }

    bool res = BundleUtil::CheckBundleJsonIsValid(bundleName, &codePath, &appId, oldVersionCode);
    bool isSystemApp = (scanFlag == SYSTEM_APP_FLAG);
    if (scanFlag != THIRD_APP_FLAG) {
        if (!res) {
            AddSystemAppPathList(nullptr, appPath, systemPathList, isSystemApp, false, appId);
            AdapterFree(appId);
            AdapterFree(codePath);
            return;
        }
        if (oldVersionCode < versionCode) {
            AddSystemAppPathList(codePath, appPath, systemPathList, isSystemApp, true, appId);
            AdapterFree(appId);
            AdapterFree(codePath);
            return;
        }
    } else {
        if (!res && !BundleUtil::CheckBundleJsonIsValid(bundleName, &codePath, &appId, oldVersionCode)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] ReloadEntireBundleInfo CheckBundleJsonIsValid failed!");
            RecordAbiityInfoEvt(bundleName);
            APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_PARSE_JSON_FALIED);
            AdapterFree(appId);
            AdapterFree(codePath);
            BundleUtil::RemoveDir(appPath);
            return;
        }
    }
#ifdef BC_TRANS_ENABLE
    TransformJsToBcWhenRestart(codePath, bundleName);
#endif
    bool ret = ReloadBundleInfo(codePath, appId, isSystemApp);
    if (ret && (scanFlag == THIRD_APP_FLAG)) {
        installedThirdBundleNum_++;
    }
    AdapterFree(appId);
    AdapterFree(codePath);
}

void GtManagerService::AddSystemAppPathList(const char *installedPath, const char *path,
    List<ToBeInstalledApp *> *systemPathList, bool isSystemApp, bool isUpdated, const char *appId)
{
    if (path == nullptr || systemPathList == nullptr) {
        return;
    }

    ToBeInstalledApp *toBeInstalledApp =
        reinterpret_cast<ToBeInstalledApp *>(UI_Malloc(sizeof(ToBeInstalledApp)));
    if (toBeInstalledApp == nullptr) {
        return;
    }
    toBeInstalledApp->installedPath = Utils::Strdup(installedPath);
    toBeInstalledApp->path = Utils::Strdup(path);
    toBeInstalledApp->isSystemApp = isSystemApp;
    toBeInstalledApp->isUpdated = isUpdated;
    toBeInstalledApp->appId = Utils::Strdup(appId);
    systemPathList->PushBack(toBeInstalledApp);
}

bool GtManagerService::ReloadBundleInfo(const char *profileDir, const char *appId, bool isSystemApp)
{
    if (profileDir == nullptr) {
        return false;
    }
    BundleRes *bundleRes = reinterpret_cast<BundleRes *>(AdapterMalloc(sizeof(BundleRes)));
    if (bundleRes == nullptr) {
        return false;
    }
    if (memset_s(bundleRes, sizeof(BundleRes), 0, sizeof(BundleRes)) != EOK) {
        AdapterFree(bundleRes);
        return false;
    }
    BundleInfo *bundleInfo = GtBundleParser::ParseHapProfile(profileDir, bundleRes);
    if (bundleInfo != nullptr) {
        bundleInfo->isSystemApp = isSystemApp;
        bundleInfo->appId = Utils::Strdup(appId);
        if (bundleInfo->appId == nullptr) {
            BundleInfoUtils::FreeBundleInfo(bundleInfo);
            AdapterFree(bundleRes->abilityRes);
            AdapterFree(bundleRes);
            return false;
        }
        bundleMap_->Add(bundleInfo);
        if (bundleRes->abilityRes == nullptr ||
            (bundleRes->abilityRes->labelId == 0 && bundleRes->abilityRes->iconId == 0)) {
            AdapterFree(bundleRes->abilityRes);
            AdapterFree(bundleRes);
        } else {
            bundleRes->bundleName = bundleInfo->bundleName;
            AddBundleResList(bundleRes);
        }
        return true;
    }
    AdapterFree(bundleRes->abilityRes);
    AdapterFree(bundleRes);
    APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_PARSE_PROFILE_FALIED);
    HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] reload bundle info fail!, isSystemApp is %d", isSystemApp);
    return false;
}

void GtManagerService::AddBundleResList(const BundleRes *bundleRes)
{
    if (bundleRes == nullptr || bundleRes->bundleName == nullptr || bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res != nullptr && res->bundleName != nullptr && strcmp(res->bundleName, bundleRes->bundleName) == 0) {
            return;
        }
    }
    bundleResList_->PushFront(const_cast<BundleRes *>(bundleRes));
}

void GtManagerService::RemoveBundleResList(const char *bundleName)
{
    if (bundleName == nullptr || bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res == nullptr) {
            return;
        }
        if (res->bundleName != nullptr && strcmp(bundleName, res->bundleName) == 0) {
            AdapterFree(res->abilityRes);
            AdapterFree(res);
            bundleResList_->Remove(node);
            return;
        }
    }
}

void GtManagerService::UpdateBundleInfoList()
{
    if (bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res == nullptr || res->bundleName == nullptr || res->abilityRes == nullptr) {
            continue;
        }

        BundleInfo *bundleInfo = bundleMap_->Get(res->bundleName);
        if (bundleInfo == nullptr) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get no bundleInfo when change bundle res!");
            continue;
        }

        int32_t len = strlen(INSTALL_PATH) + 1 + strlen(res->bundleName);
        char *path = reinterpret_cast<char *>(UI_Malloc(len + 1));
        if (path == nullptr) {
            continue;
        }

        if (sprintf_s(path, len + 1, "%s/%s", INSTALL_PATH, res->bundleName) < 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] change bundle res failed! because sprintf_s fail");
            UI_Free(path);
            continue;
        }

        uint8_t errorCode = GtBundleParser::ConvertResInfoToBundleInfo(path, res->abilityRes->labelId,
            res->abilityRes->iconId, bundleInfo);
        UI_Free(path);
        if (errorCode != ERR_OK) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] change bundle res failed! errorCode is %d", errorCode);
            return;
        }
    }
}

#ifdef BC_TRANS_ENABLE
void GtManagerService::TransformJsToBcWhenRestart(const char *codePath, const char *bundleName)
{
    if (codePath == nullptr) {
        return;
    }

    char *bundleJsonPathComp[] = {
        const_cast<char *>(JSON_PATH), const_cast<char *>(bundleName), const_cast<char *>(JSON_SUFFIX)
    };
    char *bundleJsonPath = BundleUtil::Strscat(bundleJsonPathComp, sizeof(bundleJsonPathComp) / sizeof(char *));
    if (bundleJsonPath == nullptr) {
        return;
    }

    cJSON *installRecordJson = BundleUtil::GetJsonStream(bundleJsonPath);
    if (installRecordJson == nullptr) {
        AdapterFree(bundleJsonPath);
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get installRecord fail when restart!");
        return;
    }

    if (jsEngineVer_ == nullptr) {
        cJSON_Delete(installRecordJson);
        AdapterFree(bundleJsonPath);
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] TransformJsToBcWhenRestart jsEngineVer_ is nullptr!");
        return;
    }

    cJSON *jsEngineVerObj = cJSON_CreateString(jsEngineVer_);
    if (jsEngineVerObj == nullptr) {
        cJSON_Delete(installRecordJson);
        AdapterFree(bundleJsonPath);
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] create string object fail when restart!");
        return;
    }

    cJSON *oldJsEngineVerObj = cJSON_GetObjectItem(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION);
    if (oldJsEngineVerObj == nullptr) {
        cJSON_Delete(jsEngineVerObj);
        cJSON_Delete(installRecordJson);
        AdapterFree(bundleJsonPath);
        return;
    }
    if (cJSON_IsString(oldJsEngineVerObj) && strcmp(oldJsEngineVerObj->valuestring, jsEngineVer_) == 0) {
        cJSON_Delete(jsEngineVerObj);
        cJSON *transformResultObj = cJSON_GetObjectItem(installRecordJson, JSON_SUB_KEY_TRANSFORM_RESULT);
        if (cJSON_IsNumber(transformResultObj) && transformResultObj->valueint == 0) {
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    } else if (oldJsEngineVerObj == nullptr) {
        if (!cJSON_AddItemToObject(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION, jsEngineVerObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] add js engine version record fail when restart!");
            cJSON_Delete(jsEngineVerObj);
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    } else {
        if (!cJSON_ReplaceItemInObject(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION, jsEngineVerObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] refresh js engine version fail when restart!");
            cJSON_Delete(jsEngineVerObj);
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    }

    TransformJsToBc(codePath, bundleJsonPath, installRecordJson);
    cJSON_Delete(installRecordJson);
    AdapterFree(bundleJsonPath);
}

void GtManagerService::TransformJsToBc(const char *codePath, const char *bundleJsonPath, cJSON *installRecordObj)
{
    if (codePath == nullptr || installRecordObj == nullptr || bundleJsonPath == nullptr) {
        return;
    }

    char *jsPathComp[] = {const_cast<char *>(codePath), const_cast<char *>(ASSET_JS_PATH)};
    char *jsPath = BundleUtil::Strscat(jsPathComp, sizeof(jsPathComp) / sizeof(char *));
    if (jsPath == nullptr) {
        return;
    }

    EXECRES result = walk_directory(jsPath);
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] transform js to bc, result is %d", result);
    if (result != EXCE_ACE_JERRY_EXEC_OK) {
        result = walk_del_bytecode(jsPath);
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] delete byte code, result is %d", result);
        AdapterFree(jsPath);
        return;
    }
    AdapterFree(jsPath);

    cJSON *resultObj = cJSON_CreateNumber(0);
    if (resultObj == nullptr) {
        return;
    }
    cJSON *oldResultObj = cJSON_GetObjectItem(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT);
    if (oldResultObj == nullptr) {
        if (!cJSON_AddItemToObject(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT, resultObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] add transform result record fail when restart!");
            cJSON_Delete(resultObj);
            return;
        }
    } else {
        if (!cJSON_ReplaceItemInObject(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT, resultObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] refresh transform result record fail when restart!");
            cJSON_Delete(resultObj);
            return;
        }
    }
    (void)BundleUtil::StoreJsonContentToFile(bundleJsonPath, installRecordObj);
}
#endif

bool GtManagerService::CheckThirdSystemBundleHasUninstalled(const char *bundleName, const cJSON *object)
{
    if (object == nullptr || bundleName == nullptr) {
        return false;
    }

    cJSON *array = cJSON_GetObjectItem(object, JSON_MAIN_KEY);
    if (!cJSON_IsArray(array)) {
        return false;
    }

    int32_t size = cJSON_GetArraySize(array);
    for (int32_t i = 0; i < size; i++) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsString(item)) {
            return false;
        }
        if ((item->valuestring != nullptr) && strcmp(bundleName, item->valuestring) == 0) {
            return true;
        }
    }
    return false;
}

BundleInfo *GtManagerService::QueryBundleInfo(const char *bundleName)
{
    if (bundleName == nullptr || bundleMap_ == nullptr) {
        return nullptr;
    }
    return bundleMap_->Get(bundleName);
}

void GtManagerService::RemoveBundleInfo(const char *bundleName)
{
    if (bundleName == nullptr || bundleMap_ == nullptr) {
        return;
    }
    bundleMap_->Erase(bundleName);
}

void GtManagerService::AddBundleInfo(BundleInfo *info)
{
    if (info == nullptr || info->bundleName == nullptr || bundleMap_ == nullptr) {
        return;
    }
    bundleMap_->Add(info);
}

bool GtManagerService::UpdateBundleInfo(BundleInfo *info)
{
    if (info == nullptr) {
        return false;
    }
    return bundleMap_->Update(info);
}

uint32_t GtManagerService::GetNumOfThirdBundles()
{
    return installedThirdBundleNum_;
}

void GtManagerService::AddNumOfThirdBundles()
{
    installedThirdBundleNum_++;
}

void GtManagerService::ReduceNumOfThirdBundles()
{
    installedThirdBundleNum_--;
}

bool GtManagerService::RegisterEvent(InstallerCallback listenCallback)
{
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] RegisterListen RegisterEvent");
    if (listenCallback == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] RegisterListen is fail because is null");
        return false;
    }
    listenList_->PushBack(listenCallback);
    return true;
}

bool GtManagerService::UnregisterEvent(InstallerCallback listenCallback)
{
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] UnregisterListen UnregisterEvent");
    if (listenCallback == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] UnregisterListen is fail because is null");
        return false;
    }
    for (auto node = listenList_->Begin(); node != listenList_->End(); node = node->next_) {
        if (node == nullptr) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] Listener not found");
            return false;
        }
        if ((*(node->value_)) == listenCallback) {
            listenList_->Remove(node);
            return true;
        }
    }
    return false;
}
int32_t GtManagerService::ReportHceInstallCallback(uint8_t errCode, uint8_t installState, uint8_t process)
{
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] reportListener ReportHceInstallCallback");
    BundleInstallMsg *bundleInstallMsg = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (memset_s(bundleInstallMsg, sizeof(BundleInstallMsg), 0, sizeof(BundleInstallMsg)) != EOK) {
        AdapterFree(bundleInstallMsg);
    }
    if (bundleInstallMsg == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] ReportHceInstallCallback is fail because bundleInstallMsg is null");
        return -1;
    }
    bundleInstallMsg_->installState = static_cast<InstallState>(installState);
    bundleInstallMsg_->installProcess = process;
    bundleInstallMsg->installState = bundleInstallMsg_->installState;
    bundleInstallMsg->installProcess = bundleInstallMsg_->installProcess;
    bundleInstallMsg->bundleName = bundleInstallMsg_->bundleName;
    for (auto node = listenList_->Begin(); node != listenList_->End(); node = node->next_) {
        (*(node->value_))(errCode, bundleInstallMsg);
    }
    return 0;
}

int32_t GtManagerService::ReportHceUninstallCallback(
    uint8_t errCode, uint8_t installState, char *bundleName, uint8_t process)
{
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] unReportListener ReportHceUninstallCallback");
    BundleInstallMsg *bundleInstallMsg = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (memset_s(bundleInstallMsg, sizeof(BundleInstallMsg), 0, sizeof(BundleInstallMsg)) != EOK) {
        AdapterFree(bundleInstallMsg);
    }
    if (bundleInstallMsg == nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] ReportHceUninstallCallback is fail because bundleInstallMsg is null");
        return -1;
    }
    bundleInstallMsg->installState = static_cast<InstallState>(installState);
    bundleInstallMsg->installProcess = process;
    bundleInstallMsg->bundleName = bundleName;
    for (auto node = listenList_->Begin(); node != listenList_->End(); node = node->next_) {
        (*(node->value_))(errCode, bundleInstallMsg);
    }
    return 0;
}

int32_t GtManagerService::ReportInstallCallback(
    uint8_t errCode, uint8_t installState, uint8_t process, InstallerCallback installerCallback)
{
    if (bundleInstallMsg_ == nullptr) {
        return -1;
    }
    if (installerCallback == nullptr) {
        return -1;
    }
    BundleInstallMsg *bundleInstallMsg = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (bundleInstallMsg == nullptr) {
        return -1;
    }
    bundleInstallMsg_->installState = static_cast<InstallState>(installState);
    bundleInstallMsg_->installProcess = process;
    bundleInstallMsg->installState = bundleInstallMsg_->installState;
    bundleInstallMsg->installProcess = bundleInstallMsg_->installProcess;
    bundleInstallMsg->label = bundleInstallMsg_->label;
    bundleInstallMsg->bundleName = bundleInstallMsg_->bundleName;
    bundleInstallMsg->smallIconPath = bundleInstallMsg_->smallIconPath;
    bundleInstallMsg->bigIconPath = bundleInstallMsg_->bigIconPath;
    (*installerCallback)(errCode, bundleInstallMsg);
    return 0;
}

int32_t GtManagerService::ReportUninstallCallback(uint8_t errCode, uint8_t installState, char *bundleName,
    uint8_t process, InstallerCallback installerCallback)
{
    if (installerCallback == nullptr) {
        return -1;
    }
    BundleInstallMsg *bundleInstallMsg = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (bundleInstallMsg == nullptr) {
        return -1;
    }
    bundleInstallMsg->installState = static_cast<InstallState>(installState);
    bundleInstallMsg->bundleName = bundleName;
    bundleInstallMsg->installProcess = process;
    (*installerCallback)(errCode, bundleInstallMsg);
    return 0;
}

PreAppList *GtManagerService::InitPreAppInfo()
{
    PreAppList *list = (PreAppList *)AdapterMalloc(sizeof(PreAppList));
    if (list == nullptr) {
        return nullptr;
    }

    if (memset_s(list, sizeof(PreAppList), 0, sizeof(PreAppList)) != EOK) {
        AdapterFree(list);
        return nullptr;
    }

    LOS_ListInit(&list->appDoubleList);
    return list;
}

void GtManagerService::QueryPreAppInfo(const char *appDir, PreAppList *list)
{
    struct dirent *ent = nullptr;
    if (appDir == nullptr) {
        return;
    }

    DIR *dir = opendir(appDir);
    if (dir == nullptr) {
        return;
    }
    char *fileName = reinterpret_cast<char *>(AdapterMalloc(MAX_NAME_LEN + 1));
    if (fileName == nullptr) {
        closedir(dir);
        return;
    }
    while ((ent = readdir(dir)) != nullptr) {
        if (memset_s(fileName, MAX_NAME_LEN + 1, 0, MAX_NAME_LEN + 1) != EOK) {
            break;
        }

        if (strcpy_s(fileName, MAX_NAME_LEN + 1, ent->d_name) != 0) {
            break;
        }

        if ((strcmp(fileName, ".") == 0) || (strcmp(fileName, "..")) == 0) {
            continue;
        }

        int32_t len = strlen(appDir) + 1 + strlen(fileName) + 1;
        char *appPath = reinterpret_cast<char *>(AdapterMalloc(len));
        if (appPath == nullptr) {
            break;
        }

        if (sprintf_s(appPath, len, "%s/%s", appDir, fileName) < 0) {
            AdapterFree(appPath);
            break;
        }

        if (!BundleUtil::IsFile(appPath)) {
            AdapterFree(appPath);
            continue;
        }

        InsertPreAppInfo(appPath, (PreAppList *)&list->appDoubleList);
        AdapterFree(appPath);
    }
    AdapterFree(fileName);
    closedir(dir);
}

void GtManagerService::InsertPreAppInfo(const char *filePath, PreAppList *list)
{
    if ((filePath == nullptr) || (list == nullptr)) {
        return;
    }

    PreAppList *app = (PreAppList *)AdapterMalloc(sizeof(PreAppList));
    if (app == nullptr) {
        return;
    }

    if (memset_s(app, sizeof(PreAppList), 0, sizeof(PreAppList)) != 0) {
        AdapterFree(app);
        return;
    }

    if (memcpy_s(app->filePath, sizeof(app->filePath), filePath, strnlen(filePath, MAX_APP_FILE_PATH_LEN)) != 0) {
        AdapterFree(app);
        return;
    }

    LOS_ListTailInsert(&list->appDoubleList, &app->appDoubleList);
    return;
}

void GtManagerService::SetPreAppInfo(PreAppList *list)
{
    if (list == nullptr) {
        return;
    }
    preAppList_ = list;
    return;
}

void GtManagerService::FreePreAppInfo(const PreAppList *list)
{
    if (list == nullptr) {
        return;
    }

    PreAppList *currentNode = nullptr;
    PreAppList *nextNode = nullptr;
    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(currentNode, nextNode, &list->appDoubleList, PreAppList, appDoubleList) {
        if (currentNode != nullptr) {
            LOS_ListDelete(&(currentNode->appDoubleList));
            AdapterFree(currentNode);
            currentNode = nullptr;
        }
    }

    if (list != nullptr) {
        AdapterFree(list);
    }
    return;
}
} // namespace OHOS
extern "C" {
static char *g_currentBundle = nullptr;
const int32_t BUNDLENAME_MUTEX_TIMEOUT = 2000;
static osMutexId_t g_currentBundleMutex;

void SetCurrentBundle(const char *name)
{
    MutexAcquire(&g_currentBundleMutex, BUNDLENAME_MUTEX_TIMEOUT);
    AdapterFree(g_currentBundle);
    if (name == nullptr) {
        MutexRelease(&g_currentBundleMutex);
        return;
    }

    int len = strlen(name);
    g_currentBundle = (char *)AdapterMalloc(len + 1);
    if (g_currentBundle == nullptr || strncpy_s(g_currentBundle, len + 1, name, len) < 0) {
        AdapterFree(g_currentBundle);
    }
    MutexRelease(&g_currentBundleMutex);
}

const char *GetCurrentBundle()
{
    MutexAcquire(&g_currentBundleMutex, BUNDLENAME_MUTEX_TIMEOUT);
    if (g_currentBundle == nullptr) {
        MutexRelease(&g_currentBundleMutex);
        return nullptr;
    }

    int len = strlen(g_currentBundle);
    char *bundleName = (char *)AdapterMalloc(len + 1);
    if (bundleName == nullptr || strncpy_s(bundleName, len + 1, g_currentBundle, len) < 0) {
        AdapterFree(bundleName);
        MutexRelease(&g_currentBundleMutex);
        return nullptr;
    }

    MutexRelease(&g_currentBundleMutex);
    return bundleName;
}
}
