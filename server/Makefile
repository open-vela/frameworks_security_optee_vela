#
# Copyright (C) 2023 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/optee/optee_nuttx/optee_nuttx/compat/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/optee/optee_os/optee_os/core/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/optee/optee_os/optee_os/lib/libutils/ext/include
PROGNAME = $(CONFIG_OPTEE_SERVER_PROGNAME)
PRIORITY = $(CONFIG_OPTEE_SERVER_PRIORITY)
STACKSIZE = $(CONFIG_OPTEE_SERVER_STACKSIZE)
MAINSRC := opteed.c

include $(APPDIR)/Application.mk
