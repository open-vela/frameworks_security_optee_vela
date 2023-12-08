/*
 * Copyright (C) 2020-2023 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __ERROR_MESSAGES__
#define __ERROR_MESSAGES__

/* #define PLAINTEXT_ERR_MSG */
#ifdef PLAINTEXT_ERR_MSG
    #define ERR_MSG_SUCCESS                 "success"
    #define ERR_MSG_CORRUPT_OBJECT          "corrupt object"
    #define ERR_MSG_CORRUPT_OBJECT_2        "corrupt object 2"
    #define ERR_MSG_STORAGE_NOT_AVAILABLE   "storage not available"
    #define ERR_MSG_STORAGE_NOT_AVAILABLE_2 "storage not available 2"
    #define ERR_MSG_OLD_VERSION             "old version"
    #define ERR_MSG_CIPHERTEXT_INVALID      "ciphertext invalid"
    #define ERR_MSG_GENERIC                 "generic"
    #define ERR_MSG_ACCESS_DENIED           "access denied"
    #define ERR_MSG_CANCEL                  "cancel"
    #define ERR_MSG_ACCESS_CONFLICT         "access conflict"
    #define ERR_MSG_EXCESS_DATA             "excess data"
    #define ERR_MSG_BAD_FORMAT              "bad format"
    #define ERR_MSG_BAD_PARAMETERS          "bad parameters"
    #define ERR_MSG_BAD_STATE               "bad state"
    #define ERR_MSG_ITEM_NOT_FOUND          "item not found"
    #define ERR_MSG_NOT_IMPLEMENTED         "not implemented"
    #define ERR_MSG_NOT_SUPPORTED           "not supported"
    #define ERR_MSG_NO_DATA                 "no data"
    #define ERR_MSG_OUT_OF_MEMORY           "out of memory"
    #define ERR_MSG_BUSY                    "busy"
    #define ERR_MSG_COMMUNICATION           "communication"
    #define ERR_MSG_SECURITY                "security"
    #define ERR_MSG_SHORT_BUFFER            "short buffer"
    #define ERR_MSG_EXTERNAL_CANCEL         "external cancel"
    #define ERR_MSG_TIMEOUT                 "timeout"
    #define ERR_MSG_OVERFLOW                "overflow"
    #define ERR_MSG_TARGET_DEAD             "target dead"
    #define ERR_MSG_STORAGE_NO_SPACE        "storage no space"
    #define ERR_MSG_MAC_INVALID             "mac invalid"
    #define ERR_MSG_SIGNATURE_INVALID       "signature invalid"
    #define ERR_MSG_TIME_NOT_SET            "time not set"
    #define ERR_MSG_TIME_NEEDS_RESET        "time needs reset"
#else
    /* generating tool: md5sum */
    #define ERR_MSG_SUCCESS                 "0585f49b"
    #define ERR_MSG_CORRUPT_OBJECT          "79c3f6be"
    #define ERR_MSG_CORRUPT_OBJECT_2        "b873cb3e"
    #define ERR_MSG_STORAGE_NOT_AVAILABLE   "2b8babc6"
    #define ERR_MSG_STORAGE_NOT_AVAILABLE_2 "dddf77e6"
    #define ERR_MSG_OLD_VERSION             "af45a51f"
    #define ERR_MSG_CIPHERTEXT_INVALID      "dfb849f5"
    #define ERR_MSG_GENERIC                 "36185aaf"
    #define ERR_MSG_ACCESS_DENIED           "cd90eb7a"
    #define ERR_MSG_CANCEL                  "a10bf797"
    #define ERR_MSG_ACCESS_CONFLICT         "037f0b80"
    #define ERR_MSG_EXCESS_DATA             "8143e909"
    #define ERR_MSG_BAD_FORMAT              "ec2d0a7d"
    #define ERR_MSG_BAD_PARAMETERS          "9bf8d2db"
    #define ERR_MSG_BAD_STATE               "46ff6640"
    #define ERR_MSG_ITEM_NOT_FOUND          "908cf7cc"
    #define ERR_MSG_NOT_IMPLEMENTED         "7c55dab0"
    #define ERR_MSG_NOT_SUPPORTED           "82238416"
    #define ERR_MSG_NO_DATA                 "ad6e70b2"
    #define ERR_MSG_OUT_OF_MEMORY           "16899bc8"
    #define ERR_MSG_BUSY                    "c549f06c"
    #define ERR_MSG_COMMUNICATION           "52c30453"
    #define ERR_MSG_SECURITY                "69abde01"
    #define ERR_MSG_SHORT_BUFFER            "7995725f"
    #define ERR_MSG_EXTERNAL_CANCEL         "11b4fb63"
    #define ERR_MSG_TIMEOUT                 "b366fb7c"
    #define ERR_MSG_OVERFLOW                "ef552d21"
    #define ERR_MSG_TARGET_DEAD             "295268d5"
    #define ERR_MSG_STORAGE_NO_SPACE        "bfd2eb99"
    #define ERR_MSG_MAC_INVALID             "22e9bd01"
    #define ERR_MSG_SIGNATURE_INVALID       "fd9da52d"
    #define ERR_MSG_TIME_NOT_SET            "c5c4504a"
    #define ERR_MSG_TIME_NEEDS_RESET        "290d589c"
#endif

#endif
