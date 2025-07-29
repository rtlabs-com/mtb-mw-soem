/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2025 rt-labs AB, Sweden.
 * See LICENSE file in the project root for full license information.
********************************************************************/

#ifndef SOEM_VERSION_H
#define SOEM_VERSION_H

#define SOEM_GIT_REVISION "release-v0.6.0"

#if !defined(SOEM_VERSION_BUILD) && defined(SOEM_GIT_REVISION)
#define SOEM_VERSION_BUILD SOEM_GIT_REVISION
#endif

/* clang-format-off */

#define SOEM_VERSION_MAJOR 2
#define SOEM_VERSION_MINOR 0
#define SOEM_VERSION_PATCH 0

#if defined(SOEM_VERSION_BUILD)
#define SOEM_VERSION \
   "2.0.0+"SOEM_VERSION_BUILD
#else
#define SOEM_VERSION \
   "2.0.0"
#endif

/* clang-format-on */

#endif /* SOEM_VERSION_H */
