/*
 * Copyright (c) 2020 Huawei Device Co., Ltd.
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

#ifndef OHOS_ELEMENT_NAME_UTILS_H
#define OHOS_ELEMENT_NAME_UTILS_H

#ifndef __LITEOS_M__
#include <serializer.h>
#endif
#include "element_name.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif // __cplusplus

#ifndef __LITEOS_M__
#ifdef OHOS_APPEXECFWK_BMS_BUNDLEMANAGER

bool SerializeElement(IpcIo *io, const ElementName *element);
bool DeserializeElement(ElementName *element, IpcIo *io);
#endif
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif // __cplusplus

#endif // OHOS_ELEMENT_NAME_UTILS_H
/** @} */
