# Keep upstream patch bytes pinned while encoding blank patch-context lines without
# trailing whitespace in the source repository. Materialize exact bytes in buildtrees.
function(zene_pcre2_patch name expected_sha512)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/${name}.json" encoded_patch)
    string(JSON patch_content GET "${encoded_patch}" content)
    set(path "${CURRENT_BUILDTREES_DIR}/zene-patches/${name}")
    file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/zene-patches")
    file(WRITE "${path}" "${patch_content}")
    file(SHA512 "${path}" actual_sha512)
    if(NOT actual_sha512 STREQUAL expected_sha512)
        message(FATAL_ERROR "Upstream patch ${name} SHA512 mismatch")
    endif()
    list(APPEND ZENE_PCRE2_PATCHES "${path}")
    set(ZENE_PCRE2_PATCHES "${ZENE_PCRE2_PATCHES}" PARENT_SCOPE)
endfunction()

set(ZENE_PCRE2_PATCHES "")
zene_pcre2_patch(pcre2-10.35_fix-uwp.patch
    92f231765fea9868c39552009fbe1096279e0dd9b1f121f5184ff945e0b0c7a0d6e2167976db2e2b5bbdfaf231175894fcf9221c4d94fc7d436e400847fda209)
zene_pcre2_patch(no-static-suffix.patch
    7d3eb8b71f0880efc0dd2f391d89c2a3bc4d380a335401692161f4d050d224e4ce1a827f936b23c2127118364c8c7c7e6f1270ba25161de2039be0c7d65dcb10)
zene_pcre2_patch(fix-msvc-pdb-install.patch
    824a6abee766eee1a4d5732d1a62d4bacbe56f0a372b6d81cd0a8ed94c6cf7014d2bb099e22ecd5d26421c349479d99cbd3cceed86e6358b2f33e199887856a0)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO PCRE2Project/pcre2
    REF "pcre2-${VERSION}"
    SHA512 8257285809a9a4afb441869936740ae5ff04cc4bd95f86c8e4b9fd41f3b6a194e88d0aaa1c2cfc01b8845984993736fa5e77201c82021842acc87451d8f62f8b
    HEAD_REF main
    PATCHES
        ${ZENE_PCRE2_PATCHES}
)

if("jit" IN_LIST FEATURES)
    # GitHub source snapshots omit gitlinks; .gitmodules only records paths and URLs.
    # Zene fork - cluster ci-b38398fa7833e0a4. Upstream resolves the deps/sljit gitlink of a
    # tagged release by downloading
    #   https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=pcre2-10.49
    # from the UNAUTHENTICATED GitHub REST API during `vcpkg install`. The hosted request
    # returned 403 for an undetermined reason, and vcpkg treats 403 as non-transient
    # (only 408, 429 and 5xx are retried), so Configure died in job 132369662829
    # of run 43569206372.
    # The response is a build INPUT, not a live query: the file committed beside this
    # portfile is the exact response for pcre2-10.49, verified against the same SHA512
    # upstream pins for it. Only the fetch changed - the parse, the gitlink validation and
    # the sljit source pin below are upstream's.
    if(VCPKG_USE_HEAD_VERSION)
        # A --head build asks where pcre2's deps/sljit points TODAY, so it cannot be
        # pre-committed and must query the API.
        set(pcre2_ref "${VCPKG_HEAD_VERSION}")
        set(sljit_gitlink_hash SKIP_SHA512)
        vcpkg_download_distfile(sljit_gitlink
            URLS "https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=${pcre2_ref}"
            FILENAME "pcre2-${pcre2_ref}-sljit-gitlink.json"
            ${sljit_gitlink_hash}
        )
    else()
        set(pcre2_ref "pcre2-${VERSION}")
        set(sljit_gitlink "${CMAKE_CURRENT_LIST_DIR}/sljit-gitlink-pcre2-10.49.json")
        # The SHA512 vcpkg's pcre2 port pins for this tag's gitlink response. Refreshing the
        # pcre2 version means refreshing the committed file and this value together; the
        # parsed contents are validated below as well, so a stale-but-hashed file cannot
        # point at an unexpected repository.
        set(sljit_gitlink_hash 719fa1d8d8e1e749ce6f7370db32818268f84b1376ed9367fd5290ab440d8bea7a6a25a68e55045a45da68b0ce7a55daa0d7acd2f1b11774319b50268c02441c)
        if(NOT EXISTS "${sljit_gitlink}")
            message(FATAL_ERROR "The committed deps/sljit gitlink response is missing: '${sljit_gitlink}'.")
        endif()
        file(SHA512 "${sljit_gitlink}" sljit_gitlink_actual_hash)
        if(NOT sljit_gitlink_actual_hash STREQUAL sljit_gitlink_hash)
            message(FATAL_ERROR "The committed deps/sljit gitlink response '${sljit_gitlink}' has SHA512 ${sljit_gitlink_actual_hash}, expected ${sljit_gitlink_hash} (the value vcpkg's pcre2 port pins for pcre2-${VERSION}).")
        endif()
    endif()
    file(READ "${sljit_gitlink}" sljit_gitlink_contents)
    string(JSON sljit_ref GET "${sljit_gitlink_contents}" sha)
    string(JSON sljit_url GET "${sljit_gitlink_contents}" submodule_git_url)
    string(LENGTH "${sljit_ref}" sljit_ref_length)
    if(NOT sljit_ref MATCHES "^[0-9a-f]+$" OR NOT sljit_ref_length EQUAL 40
        OR NOT sljit_url STREQUAL "https://github.com/zherczeg/sljit.git")
        message(FATAL_ERROR "Unexpected deps/sljit gitlink in PCRE2 ${pcre2_ref}: ${sljit_url} at ${sljit_ref}")
    endif()

    if(VCPKG_USE_HEAD_VERSION)
        # Fetch PCRE2's resolved gitlink, not an independently moving SLJIT HEAD.
        vcpkg_download_distfile(sljit_archive
            URLS "https://github.com/zherczeg/sljit/archive/${sljit_ref}.tar.gz"
            FILENAME "zherczeg-sljit-${sljit_ref}.tar.gz"
            SKIP_SHA512
        )
        vcpkg_extract_source_archive(SLJIT_SOURCE_PATH ARCHIVE "${sljit_archive}")
    else()
        vcpkg_from_github(
            OUT_SOURCE_PATH SLJIT_SOURCE_PATH
            REPO zherczeg/sljit
            REF "${sljit_ref}"
            SHA512 e5cc01f941877558f19859aedc9079c155f752a92e6d94fc5fb716e483b8fcb3a8495a5974bbb545d6d9a947ca97ebb36bfe278c4dc083afeb48b8ccab36895b
        )
    endif()

    file(REMOVE_RECURSE "${SOURCE_PATH}/deps/sljit")
    file(MAKE_DIRECTORY "${SOURCE_PATH}/deps")
    file(RENAME "${SLJIT_SOURCE_PATH}" "${SOURCE_PATH}/deps/sljit")
endif()

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" BUILD_STATIC_LIBS)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" INSTALL_MSVC_PDB)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        jit   PCRE2_SUPPORT_JIT
)

if(VCPKG_TARGET_IS_ANDROID)
    list(APPEND FEATURE_OPTIONS -DHAVE_VSCRIPT_GNU=FALSE)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DBUILD_STATIC_LIBS=${BUILD_STATIC_LIBS}
        -DPCRE2_BUILD_PCRE2_8=ON
        -DPCRE2_BUILD_PCRE2_16=ON
        -DPCRE2_BUILD_PCRE2_32=ON
        -DPCRE2_SUPPORT_UNICODE=ON
        -DPCRE2_BUILD_TESTS=OFF
        -DPCRE2_BUILD_PCRE2GREP=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_BZip2=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Readline=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Editline=ON
        -DINSTALL_MSVC_PDB=${INSTALL_MSVC_PDB}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})

if(BUILD_STATIC_LIBS)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/pcre2.h" "defined(PCRE2_STATIC)" "1")
else()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/pcre2.h" "defined(PCRE2_STATIC)" "0")
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/share/man"
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share")

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/pcre2")
file(RENAME "${CURRENT_PACKAGES_DIR}/bin/pcre2-config" "${CURRENT_PACKAGES_DIR}/tools/pcre2/pcre2-config")
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/tools/pcre2/pcre2-config" "${CURRENT_PACKAGES_DIR}" [[$(cd "$(dirname "$0")/../.."; pwd -P)]])
if(NOT VCPKG_BUILD_TYPE)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/pcre2/debug")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/bin/pcre2-config" "${CURRENT_PACKAGES_DIR}/tools/pcre2/debug/pcre2-config")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/tools/pcre2/debug/pcre2-config" "${CURRENT_PACKAGES_DIR}/debug" [[$(cd "$(dirname "$0")/../../../debug"; pwd -P)]])
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/tools/pcre2/debug/pcre2-config" [[${prefix}/include]] [[${prefix}/../include]])
endif()
vcpkg_clean_executables_in_bin(FILE_NAMES none)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
set(copyright_files "${SOURCE_PATH}/LICENCE.md")
if(PCRE2_SUPPORT_JIT)
    list(APPEND copyright_files "${SOURCE_PATH}/deps/sljit/LICENSE")
endif()
vcpkg_install_copyright(FILE_LIST ${copyright_files})
