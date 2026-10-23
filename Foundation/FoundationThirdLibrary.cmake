include_guard(GLOBAL)

get_filename_component(
    FOUNDATION_ROOT
    "${CMAKE_CURRENT_LIST_DIR}"
    ABSOLUTE
)

set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)

option(
    FOUNDATION_AUTO_INSTALL_THIRD_PARTY
    "Download and install missing Foundation third-party dependencies."
    ON
)

function(FoundationResolveMsvcSystemRuntime)
    if(NOT MSVC OR NOT WIN32)
        set_property(
            GLOBAL
            PROPERTY FOUNDATION_MSVC_SYSTEM_RUNTIME_LIBS
            ""
        )
        return()
    endif()

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    set(msvcSystemRuntimeLibraries
        ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
    )
    list(REMOVE_DUPLICATES msvcSystemRuntimeLibraries)

    set(hasOpenMpRuntime FALSE)
    foreach(runtimeLibrary IN LISTS msvcSystemRuntimeLibraries)
        if(NOT EXISTS "${runtimeLibrary}")
            message(FATAL_ERROR
                "Required MSVC runtime library does not exist: "
                "${runtimeLibrary}"
            )
        endif()
        get_filename_component(runtimeName "${runtimeLibrary}" NAME)
        if(runtimeName MATCHES "^vcomp[0-9]+\\.dll$")
            set(hasOpenMpRuntime TRUE)
        endif()
    endforeach()

    if(NOT hasOpenMpRuntime)
        message(FATAL_ERROR
            "The MSVC OpenMP runtime was not resolved. "
            "FoundationAgentLocal requires it through GraphicsMagick."
        )
    endif()

    set_property(
        GLOBAL
        PROPERTY FOUNDATION_MSVC_SYSTEM_RUNTIME_LIBS
        "${msvcSystemRuntimeLibraries}"
    )
endfunction()

FoundationResolveMsvcSystemRuntime()

set(
    FOUNDATION_VCPKG_COMMIT
    "f87344cac03158cbf1467264565f1fd36b382a24"
    CACHE STRING
    "Pinned vcpkg commit used by the Foundation solution."
)
set(
    FOUNDATION_DEPS_DIR
    "${FOUNDATION_ROOT}/../Bin/Deps"
    CACHE PATH
    "Dependency root owned by the Foundation solution."
)
set(
    FOUNDATION_VCPKG_ROOT
    "${FOUNDATION_DEPS_DIR}/vcpkg"
    CACHE PATH
    "Pinned vcpkg checkout owned by Foundation."
)
set(
    FOUNDATION_VCPKG_BINARY_CACHE
    "${FOUNDATION_DEPS_DIR}/vcpkg-binary-cache"
    CACHE PATH
    "Binary cache used by Foundation vcpkg installs."
)
set(
    FOUNDATION_VCPKG_OVERLAY_TRIPLETS
    ""
    CACHE PATH
    "Optional directory containing Foundation-owned vcpkg target triplets."
)
set(
    FOUNDATION_APACHE_TIKA_VERSION
    "3.3.2"
    CACHE STRING
    "Pinned Apache Tika application version used for local document extraction."
)
set(
    FOUNDATION_TEMURIN_JRE_VERSION
    "21.0.11+10"
    CACHE STRING
    "Pinned Eclipse Temurin JRE version used by Apache Tika."
)
set(
    FOUNDATION_AGENT_OFFICE_PYTHON_VERSION
    "3.12.10"
    CACHE STRING
    "Pinned embedded Python version used by FoundationAgent Office tools."
)
set(
    FOUNDATION_AGENT_FFMPEG_BUILD
    "N-125365-g9a01c1cb6a"
    CACHE STRING
    "Pinned BtbN LGPL FFmpeg build used by FoundationAgent media preprocessing."
)
set(
    FOUNDATION_AGENT_FFMPEG_RELEASE_TAG
    "autobuild-2026-06-30-13-34"
    CACHE STRING
    "Pinned BtbN FFmpeg release tag used by FoundationAgent."
)

function(FoundationDetectVcpkgArchitecture outArchitecture)
    set(platformArchitecture "${CMAKE_GENERATOR_PLATFORM}")
    if("${platformArchitecture}" STREQUAL "" AND DEFINED CMAKE_VS_PLATFORM_NAME)
        set(platformArchitecture "${CMAKE_VS_PLATFORM_NAME}")
    endif()
    if("${platformArchitecture}" STREQUAL "" AND APPLE AND DEFINED CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES architectureCount)
        if(architectureCount GREATER 1)
            message(FATAL_ERROR
                "Foundation vcpkg integration does not support a universal macOS build in one tree. "
                "Configure one build directory per CMAKE_OSX_ARCHITECTURES value."
            )
        elseif(architectureCount EQUAL 1)
            set(platformArchitecture "${CMAKE_OSX_ARCHITECTURES}")
        endif()
    endif()
    if("${platformArchitecture}" STREQUAL "")
        set(platformArchitecture "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if("${platformArchitecture}" STREQUAL "")
        set(platformArchitecture "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    string(TOLOWER "${platformArchitecture}" normalizedArchitecture)
    if(normalizedArchitecture MATCHES "^(x64|amd64|x86_64)$")
        set(vcpkgArchitecture "x64")
    elseif(normalizedArchitecture MATCHES "^(arm64|aarch64)$")
        set(vcpkgArchitecture "arm64")
    elseif(normalizedArchitecture MATCHES "^(win32|x86|i[3-6]86)$")
        set(vcpkgArchitecture "x86")
    elseif(normalizedArchitecture MATCHES "^(arm|armv7|armv7l)$")
        set(vcpkgArchitecture "arm")
    else()
        message(FATAL_ERROR
            "Foundation cannot map platform architecture '${platformArchitecture}' to a vcpkg triplet. "
            "Set VCPKG_TARGET_TRIPLET explicitly."
        )
    endif()

    set(${outArchitecture} "${vcpkgArchitecture}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED VCPKG_TARGET_TRIPLET OR "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
    FoundationDetectVcpkgArchitecture(vcpkgArchitecture)
    if(WIN32)
        set(detectedTriplet "${vcpkgArchitecture}-windows")
    elseif(APPLE)
        set(detectedTriplet "${vcpkgArchitecture}-osx")
    elseif(UNIX)
        set(detectedTriplet "${vcpkgArchitecture}-linux")
    else()
        message(FATAL_ERROR "Foundation cannot select a vcpkg triplet for this platform.")
    endif()
    set(VCPKG_TARGET_TRIPLET "${detectedTriplet}" CACHE STRING "Foundation vcpkg target triplet.")
endif()

set(
    FOUNDATION_VCPKG_INSTALLED_DIR
    "${FOUNDATION_VCPKG_ROOT}/installed"
    CACHE INTERNAL
    "Foundation vcpkg installed directory."
    FORCE
)

function(FoundationRunThirdPartyCommand description)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE commandResult
        OUTPUT_VARIABLE commandOutput
        ERROR_VARIABLE commandError
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
    )
    if(NOT "${commandResult}" STREQUAL "0")
        message(FATAL_ERROR
            "${description} failed with exit code ${commandResult}.\n"
            "${commandOutput}\n${commandError}"
        )
    endif()
endfunction()

function(FoundationDownloadPinnedFile description url expectedHash destinationPath)
    get_filename_component(destinationDirectory "${destinationPath}" DIRECTORY)
    file(MAKE_DIRECTORY "${destinationDirectory}")
    string(LENGTH "${expectedHash}" expectedHashLength)
    if(expectedHashLength EQUAL 64)
        set(hashAlgorithm "SHA256")
    elseif(expectedHashLength EQUAL 128)
        set(hashAlgorithm "SHA512")
    else()
        message(FATAL_ERROR "${description} uses an unsupported checksum length: ${expectedHashLength}")
    endif()
    if(EXISTS "${destinationPath}")
        file(${hashAlgorithm} "${destinationPath}" currentHash)
        if("${currentHash}" STREQUAL "${expectedHash}")
            return()
        endif()
        if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
            message(FATAL_ERROR "${description} exists but its ${hashAlgorithm} does not match the pinned artifact: ${destinationPath}")
        endif()
    elseif(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        message(FATAL_ERROR "${description} is missing while FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF: ${destinationPath}")
    endif()

    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef downloadSuffix)
    set(downloadPath "${destinationPath}.${downloadSuffix}.download")
    file(DOWNLOAD
        "${url}"
        "${downloadPath}"
        EXPECTED_HASH "${hashAlgorithm}=${expectedHash}"
        SHOW_PROGRESS
        STATUS downloadStatus
        TLS_VERIFY ON
    )
    list(GET downloadStatus 0 downloadResult)
    list(GET downloadStatus 1 downloadMessage)
    if(NOT downloadResult EQUAL 0)
        file(REMOVE "${downloadPath}")
        message(FATAL_ERROR "${description} download failed: ${downloadMessage}")
    endif()
    if(EXISTS "${destinationPath}")
        file(REMOVE "${destinationPath}")
    endif()
    file(RENAME "${downloadPath}" "${destinationPath}")
endfunction()

function(FoundationResolveAgentDocumentThirdParty)
    FoundationDetectVcpkgArchitecture(documentArchitecture)

    set(documentRoot "${FOUNDATION_DEPS_DIR}/DocumentExtractor")
    file(MAKE_DIRECTORY "${documentRoot}")

    set(tikaFileName "tika-app-${FOUNDATION_APACHE_TIKA_VERSION}.jar")
    set(tikaPath "${documentRoot}/ApacheTika/${FOUNDATION_APACHE_TIKA_VERSION}/${tikaFileName}")
    FoundationDownloadPinnedFile(
        "Apache Tika ${FOUNDATION_APACHE_TIKA_VERSION}"
        "https://dlcdn.apache.org/tika/${FOUNDATION_APACHE_TIKA_VERSION}/${tikaFileName}"
        "88c2032cba0d45feea361e6eebd2918bd04707614cdda5d89a1b167da5503c98e7b4cd368336f0402d559abcaf5006fcc7c825c32c749ae0417ea2f3b8423aba"
        "${tikaPath}"
    )

    string(REPLACE "+" "_" temurinFileVersion "${FOUNDATION_TEMURIN_JRE_VERSION}")
    if(WIN32)
        if(NOT "${documentArchitecture}" STREQUAL "x64")
            message(FATAL_ERROR
                "Foundation Agent document extraction currently supports only "
                "Windows x64."
            )
        endif()
        set(temurinFileName
            "OpenJDK21U-jre_x64_windows_hotspot_${temurinFileVersion}.zip"
        )
        set(temurinHash
            "be26677aaa20b39a62edcaab4c8857a8b76673b0f45abc0b6143b142b62717e4"
        )
        set(javaExecutableName "java.exe")
    elseif(APPLE)
        if("${documentArchitecture}" STREQUAL "arm64")
            set(temurinArchitecture "aarch64")
            set(temurinHash
                "4b7a8cd23102c251c8b8be42a9a5f1263fb337cf1037f6f64b25f3070efe4b76"
            )
        elseif("${documentArchitecture}" STREQUAL "x64")
            set(temurinArchitecture "x64")
            set(temurinHash
                "b341fb8ed5b70d49066b98176bc98e30f55082192403deb60e0cd5948b6e7923"
            )
        else()
            message(FATAL_ERROR
                "Foundation Agent document extraction does not support macOS "
                "architecture '${documentArchitecture}'."
            )
        endif()
        set(temurinFileName
            "OpenJDK21U-jre_${temurinArchitecture}_mac_hotspot_${temurinFileVersion}.tar.gz"
        )
        set(javaExecutableName "java")
    else()
        message(FATAL_ERROR
            "Foundation Agent document extraction does not yet support "
            "'${CMAKE_SYSTEM_NAME}'."
        )
    endif()
    set(temurinArchivePath "${documentRoot}/EclipseTemurin/${FOUNDATION_TEMURIN_JRE_VERSION}/${temurinFileName}")
    FoundationDownloadPinnedFile(
        "Eclipse Temurin JRE ${FOUNDATION_TEMURIN_JRE_VERSION}"
        "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/${temurinFileName}"
        "${temurinHash}"
        "${temurinArchivePath}"
    )

    set(temurinExtractRoot "${documentRoot}/EclipseTemurin/${FOUNDATION_TEMURIN_JRE_VERSION}/Runtime")
    file(GLOB_RECURSE javaCandidates
        LIST_DIRECTORIES false
        "${temurinExtractRoot}/${javaExecutableName}"
    )
    list(LENGTH javaCandidates javaCandidateCount)
    if(javaCandidateCount EQUAL 0)
        if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
            message(FATAL_ERROR "The pinned Eclipse Temurin JRE is not extracted: ${temurinExtractRoot}")
        endif()
        file(MAKE_DIRECTORY "${temurinExtractRoot}")
        file(ARCHIVE_EXTRACT INPUT "${temurinArchivePath}" DESTINATION "${temurinExtractRoot}")
        file(GLOB_RECURSE javaCandidates
            LIST_DIRECTORIES false
            "${temurinExtractRoot}/${javaExecutableName}"
        )
        list(LENGTH javaCandidates javaCandidateCount)
    endif()
    if(NOT javaCandidateCount EQUAL 1)
        message(FATAL_ERROR
            "Foundation could not resolve exactly one Eclipse Temurin "
            "${javaExecutableName} below: ${temurinExtractRoot}"
        )
    endif()
    list(GET javaCandidates 0 javaExecutable)
    get_filename_component(javaBinDirectory "${javaExecutable}" DIRECTORY)
    get_filename_component(temurinRuntimeRoot "${javaBinDirectory}" DIRECTORY)

    set(FOUNDATION_DOCUMENT_TIKA_JAR "${tikaPath}" CACHE INTERNAL "Pinned Apache Tika application path." FORCE)
    set(FOUNDATION_DOCUMENT_JRE_ROOT "${temurinRuntimeRoot}" CACHE INTERNAL "Pinned Eclipse Temurin JRE root." FORCE)
    set(FOUNDATION_DOCUMENT_JAVA_EXECUTABLE "${javaExecutable}" CACHE INTERNAL "Pinned Eclipse Temurin Java executable." FORCE)
endfunction()

function(FoundationDeployAgentDocumentRuntime targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "Cannot deploy document extraction runtime for missing target: ${targetName}")
    endif()
    if(NOT EXISTS "${FOUNDATION_DOCUMENT_TIKA_JAR}" OR NOT IS_DIRECTORY "${FOUNDATION_DOCUMENT_JRE_ROOT}")
        message(FATAL_ERROR "Foundation Agent document extraction dependencies have not been resolved.")
    endif()
    set(runtimeRoot "$<TARGET_FILE_DIR:${targetName}>/Tools/DocumentExtractor")
    add_custom_command(TARGET ${targetName} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${runtimeRoot}/Jre"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FOUNDATION_DOCUMENT_TIKA_JAR}"
            "${runtimeRoot}/tika-app.jar"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${FOUNDATION_DOCUMENT_JRE_ROOT}"
            "${runtimeRoot}/Jre"
        COMMENT "Deploying the pinned Foundation Agent document extraction runtime"
        VERBATIM
    )
endfunction()

function(FoundationExtractOfficeWheel wheelPath packageMarker sitePackagesDirectory)
    if(EXISTS "${sitePackagesDirectory}/${packageMarker}")
        return()
    endif()
    if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        message(FATAL_ERROR
            "The pinned Office package is not extracted while "
            "FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF: ${packageMarker}"
        )
    endif()
    file(ARCHIVE_EXTRACT
        INPUT "${wheelPath}"
        DESTINATION "${sitePackagesDirectory}"
    )
    if(NOT EXISTS "${sitePackagesDirectory}/${packageMarker}")
        message(FATAL_ERROR
            "The Office package did not provide its expected marker: ${packageMarker}"
        )
    endif()
endfunction()

function(FoundationResolveAgentOfficeThirdParty)
    FoundationDetectVcpkgArchitecture(officeArchitecture)
    set(officeRoot "${FOUNDATION_DEPS_DIR}/OfficeRuntime")
    if(WIN32)
        if(NOT "${officeArchitecture}" STREQUAL "x64")
            message(FATAL_ERROR
                "FoundationAgent Office editing currently supports only Windows x64."
            )
        endif()
        set(pythonArchiveName
            "python-${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}-embed-amd64.zip"
        )
        set(pythonArchivePath
            "${officeRoot}/Python/${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}/${pythonArchiveName}"
        )
        FoundationDownloadPinnedFile(
            "Python ${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION} embeddable runtime"
            "https://www.python.org/ftp/python/${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}/${pythonArchiveName}"
            "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3"
            "${pythonArchivePath}"
        )

        set(pythonRuntimeRoot
            "${officeRoot}/Python/${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}/Runtime"
        )
        if(NOT EXISTS "${pythonRuntimeRoot}/python.exe")
            if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
                message(FATAL_ERROR
                    "The pinned embedded Python runtime is not extracted."
                )
            endif()
            file(MAKE_DIRECTORY "${pythonRuntimeRoot}")
            file(ARCHIVE_EXTRACT
                INPUT "${pythonArchivePath}"
                DESTINATION "${pythonRuntimeRoot}"
            )
        endif()

        set(sitePackagesDirectory
            "${pythonRuntimeRoot}/Lib/site-packages"
        )
        file(MAKE_DIRECTORY "${sitePackagesDirectory}")
        file(WRITE "${pythonRuntimeRoot}/python312._pth"
            "python312.zip\n"
            ".\n"
            "Lib/site-packages\n"
            "import site\n"
        )
    elseif(APPLE)
        set(pythonStandaloneRelease "20250409")
        if("${officeArchitecture}" STREQUAL "arm64")
            set(pythonStandaloneArchitecture "aarch64")
            set(pythonStandaloneHash
                "2d6477ecd10191675b7e7979e4b9e811fef36833ef3a7f3aa445eec305ce59a2"
            )
        elseif("${officeArchitecture}" STREQUAL "x64")
            set(pythonStandaloneArchitecture "x86_64")
            set(pythonStandaloneHash
                "d0d51fa22c0e99b58de1b1b4baeca467d6fd0a1424c7509ea280c0796306c481"
            )
        else()
            message(FATAL_ERROR
                "FoundationAgent Office editing does not support macOS "
                "architecture '${officeArchitecture}'."
            )
        endif()
        set(pythonArchiveName
            "cpython-${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}+${pythonStandaloneRelease}-${pythonStandaloneArchitecture}-apple-darwin-install_only.tar.gz"
        )
        set(pythonArchivePath
            "${officeRoot}/Python/${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}/macOS-${officeArchitecture}/${pythonArchiveName}"
        )
        FoundationDownloadPinnedFile(
            "Python ${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION} standalone runtime"
            "https://github.com/astral-sh/python-build-standalone/releases/download/${pythonStandaloneRelease}/${pythonArchiveName}"
            "${pythonStandaloneHash}"
            "${pythonArchivePath}"
        )
        set(pythonExtractRoot
            "${officeRoot}/Python/${FOUNDATION_AGENT_OFFICE_PYTHON_VERSION}/macOS-${officeArchitecture}/Runtime"
        )
        set(pythonRuntimeRoot "${pythonExtractRoot}/python")
        if(NOT EXISTS "${pythonRuntimeRoot}/bin/python3")
            if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
                message(FATAL_ERROR
                    "The pinned standalone Python runtime is not extracted."
                )
            endif()
            file(MAKE_DIRECTORY "${pythonExtractRoot}")
            file(ARCHIVE_EXTRACT
                INPUT "${pythonArchivePath}"
                DESTINATION "${pythonExtractRoot}"
            )
        endif()
        set(sitePackagesDirectory
            "${pythonRuntimeRoot}/lib/python3.12/site-packages"
        )
        file(MAKE_DIRECTORY "${sitePackagesDirectory}")
    else()
        message(FATAL_ERROR
            "FoundationAgent Office editing does not yet support "
            "'${CMAKE_SYSTEM_NAME}'."
        )
    endif()

    set(wheelRoot "${officeRoot}/Wheels")

    set(pythonDocxWheel "${wheelRoot}/python_docx-1.2.0-py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "python-docx 1.2.0"
        "https://files.pythonhosted.org/packages/d0/00/1e03a4989fa5795da308cd774f05b704ace555a70f9bf9d3be057b680bcf/python_docx-1.2.0-py3-none-any.whl"
        "3fd478f3250fbbbfd3b94fe1e985955737c145627498896a8a6bf81f4baf66c7"
        "${pythonDocxWheel}"
    )
    FoundationExtractOfficeWheel("${pythonDocxWheel}" "docx/__init__.py" "${sitePackagesDirectory}")

    set(openpyxlWheel "${wheelRoot}/openpyxl-3.1.5-py2.py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "openpyxl 3.1.5"
        "https://files.pythonhosted.org/packages/c0/da/977ded879c29cbd04de313843e76868e6e13408a94ed6b987245dc7c8506/openpyxl-3.1.5-py2.py3-none-any.whl"
        "5282c12b107bffeef825f4617dc029afaf41d0ea60823bbb665ef3079dc79de2"
        "${openpyxlWheel}"
    )
    FoundationExtractOfficeWheel("${openpyxlWheel}" "openpyxl/__init__.py" "${sitePackagesDirectory}")

    set(pythonPptxWheel "${wheelRoot}/python_pptx-1.0.2-py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "python-pptx 1.0.2"
        "https://files.pythonhosted.org/packages/d9/4f/00be2196329ebbff56ce564aa94efb0fbc828d00de250b1980de1a34ab49/python_pptx-1.0.2-py3-none-any.whl"
        "160838e0b8565a8b1f67947675886e9fea18aa5e795db7ae531606d68e785cba"
        "${pythonPptxWheel}"
    )
    FoundationExtractOfficeWheel("${pythonPptxWheel}" "pptx/__init__.py" "${sitePackagesDirectory}")

    if(WIN32)
        set(lxmlFileName "lxml-5.4.0-cp312-cp312-win_amd64.whl")
        set(lxmlUrl
            "https://files.pythonhosted.org/packages/91/1e/05ddcb57ad2f3069101611bd5f5084157d90861a2ef460bf42f45cced944/${lxmlFileName}"
        )
        set(lxmlHash
            "1dc4ca99e89c335a7ed47d38964abcb36c5910790f9bd106f2a8fa2ee0b909d2"
        )
    else()
        set(lxmlFileName
            "lxml-5.4.0-cp312-cp312-macosx_10_9_universal2.whl"
        )
        set(lxmlUrl
            "https://files.pythonhosted.org/packages/f8/4c/d101ace719ca6a4ec043eb516fcfcb1b396a9fccc4fcd9ef593df34ba0d5/${lxmlFileName}"
        )
        set(lxmlHash
            "b5aff6f3e818e6bdbbb38e5967520f174b18f539c2b9de867b1e7fde6f8d95a4"
        )
    endif()
    set(lxmlWheel "${wheelRoot}/${lxmlFileName}")
    FoundationDownloadPinnedFile(
        "lxml 5.4.0"
        "${lxmlUrl}"
        "${lxmlHash}"
        "${lxmlWheel}"
    )
    FoundationExtractOfficeWheel("${lxmlWheel}" "lxml/__init__.py" "${sitePackagesDirectory}")

    if(WIN32)
        set(pillowFileName
            "pillow-11.2.1-cp312-cp312-win_amd64.whl"
        )
        set(pillowUrl
            "https://files.pythonhosted.org/packages/7c/18/24bff2ad716257fc03da964c5e8f05d9790a779a8895d6566e493ccf0189/${pillowFileName}"
        )
        set(pillowHash
            "14e33b28bf17c7a38eede290f77db7c664e4eb01f7869e37fa98a5aa95978941"
        )
    elseif("${officeArchitecture}" STREQUAL "arm64")
        set(pillowFileName
            "pillow-11.2.1-cp312-cp312-macosx_11_0_arm64.whl"
        )
        set(pillowUrl
            "https://files.pythonhosted.org/packages/e5/7e/b86dbd35a5f938632093dc40d1682874c33dcfe832558fc80ca56bfcb774/${pillowFileName}"
        )
        set(pillowHash
            "78092232a4ab376a35d68c4e6d5e00dfd73454bd12b230420025fbe178ee3b0b"
        )
    else()
        set(pillowFileName
            "pillow-11.2.1-cp312-cp312-macosx_10_13_x86_64.whl"
        )
        set(pillowUrl
            "https://files.pythonhosted.org/packages/c7/40/052610b15a1b8961f52537cc8326ca6a881408bc2bdad0d852edeb6ed33b/${pillowFileName}"
        )
        set(pillowHash
            "78afba22027b4accef10dbd5eed84425930ba41b3ea0a86fa8d20baaf19d807f"
        )
    endif()
    set(pillowWheel "${wheelRoot}/${pillowFileName}")
    FoundationDownloadPinnedFile(
        "Pillow 11.2.1"
        "${pillowUrl}"
        "${pillowHash}"
        "${pillowWheel}"
    )
    FoundationExtractOfficeWheel("${pillowWheel}" "PIL/__init__.py" "${sitePackagesDirectory}")

    set(xlsxWriterWheel "${wheelRoot}/xlsxwriter-3.2.5-py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "XlsxWriter 3.2.5"
        "https://files.pythonhosted.org/packages/fa/34/a22e6664211f0c8879521328000bdcae9bf6dbafa94a923e531f6d5b3f73/xlsxwriter-3.2.5-py3-none-any.whl"
        "4f4824234e1eaf9d95df9a8fe974585ff91d0f5e3d3f12ace5b71e443c1c6abd"
        "${xlsxWriterWheel}"
    )
    FoundationExtractOfficeWheel("${xlsxWriterWheel}" "xlsxwriter/__init__.py" "${sitePackagesDirectory}")

    set(typingExtensionsWheel "${wheelRoot}/typing_extensions-4.13.2-py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "typing-extensions 4.13.2"
        "https://files.pythonhosted.org/packages/8b/54/b1ae86c0973cc6f0210b53d508ca3641fb6d0c56823f288d108bc7ab3cc8/typing_extensions-4.13.2-py3-none-any.whl"
        "a439e7c04b49fec3e5d3e2beaa21755cadbbdc391694e28ccdd36ca4a1408f8c"
        "${typingExtensionsWheel}"
    )
    FoundationExtractOfficeWheel("${typingExtensionsWheel}" "typing_extensions.py" "${sitePackagesDirectory}")

    set(etXmlFileWheel "${wheelRoot}/et_xmlfile-2.0.0-py3-none-any.whl")
    FoundationDownloadPinnedFile(
        "et-xmlfile 2.0.0"
        "https://files.pythonhosted.org/packages/c1/8b/5fe2cc11fee489817272089c4203e679c63b570a5aaeb18d852ae3cbba6a/et_xmlfile-2.0.0-py3-none-any.whl"
        "7a91720bc756843502c3b7504c77b8fe44217c85c537d85037f0f536151b2caa"
        "${etXmlFileWheel}"
    )
    FoundationExtractOfficeWheel("${etXmlFileWheel}" "et_xmlfile/__init__.py" "${sitePackagesDirectory}")

    set(
        FOUNDATION_AGENT_OFFICE_RUNTIME_ROOT
        "${pythonRuntimeRoot}"
        CACHE INTERNAL
        "Pinned FoundationAgent Office runtime root."
        FORCE
    )
endfunction()

function(FoundationDeployAgentOfficeRuntime targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "Cannot deploy Office runtime for missing target: ${targetName}")
    endif()
    if(NOT IS_DIRECTORY "${FOUNDATION_AGENT_OFFICE_RUNTIME_ROOT}")
        message(FATAL_ERROR "FoundationAgent Office dependencies have not been resolved.")
    endif()
    set(runtimeRoot "$<TARGET_FILE_DIR:${targetName}>/Tools/Office")
    add_custom_command(TARGET ${targetName} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${runtimeRoot}/Python"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${FOUNDATION_AGENT_OFFICE_RUNTIME_ROOT}"
            "${runtimeRoot}/Python"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FOUNDATION_ROOT}/Dependencies/Agent/Tools/Office/office_bridge.py"
            "${runtimeRoot}/office_bridge.py"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FOUNDATION_ROOT}/Dependencies/Agent/Tools/Office/THIRD_PARTY_NOTICES.md"
            "${runtimeRoot}/THIRD_PARTY_NOTICES.md"
        COMMENT "Deploying the pinned FoundationAgent Office runtime"
        VERBATIM
    )
endfunction()

function(FoundationResolveAgentImageThirdParty)
    set(vcpkgPrefix "${FOUNDATION_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if(FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        get_property(dependenciesInstalled GLOBAL PROPERTY FOUNDATION_AGENT_IMAGE_DEPS_INSTALLED)
        if(NOT dependenciesInstalled)
            FoundationInstallThirdParty(graphicsmagick)
            set_property(GLOBAL PROPERTY FOUNDATION_AGENT_IMAGE_DEPS_INSTALLED TRUE)
        endif()
    endif()

    set(includeDirectory "${vcpkgPrefix}/include/GraphicsMagick")
    if(NOT IS_DIRECTORY "${includeDirectory}")
        message(FATAL_ERROR
            "FoundationAgent image editing requires GraphicsMagick headers: "
            "${includeDirectory}"
        )
    endif()

    if(WIN32)
        set(debugLibraryDirectory "${vcpkgPrefix}/debug/lib")
        set(releaseLibraryDirectory "${vcpkgPrefix}/lib")
        set(debugRuntimeDirectory "${vcpkgPrefix}/debug/bin")
        set(releaseRuntimeDirectory "${vcpkgPrefix}/bin")

        if(NOT TARGET FoundationGraphicsMagick)
            add_library(FoundationGraphicsMagick SHARED IMPORTED GLOBAL)
            set_target_properties(FoundationGraphicsMagick PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${debugLibraryDirectory}/GraphicsMagick.lib"
                IMPORTED_IMPLIB_RELEASE "${releaseLibraryDirectory}/GraphicsMagick.lib"
                IMPORTED_IMPLIB_RELWITHDEBINFO "${releaseLibraryDirectory}/GraphicsMagick.lib"
                IMPORTED_IMPLIB_MINSIZEREL "${releaseLibraryDirectory}/GraphicsMagick.lib"
                IMPORTED_LOCATION_DEBUG "${debugRuntimeDirectory}/GraphicsMagick-3.dll"
                IMPORTED_LOCATION_RELEASE "${releaseRuntimeDirectory}/GraphicsMagick-3.dll"
                IMPORTED_LOCATION_RELWITHDEBINFO "${releaseRuntimeDirectory}/GraphicsMagick-3.dll"
                IMPORTED_LOCATION_MINSIZEREL "${releaseRuntimeDirectory}/GraphicsMagick-3.dll"
            )
        endif()

        if(NOT TARGET FoundationGraphicsMagickPP)
            add_library(FoundationGraphicsMagickPP SHARED IMPORTED GLOBAL)
            set_target_properties(FoundationGraphicsMagickPP PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${debugLibraryDirectory}/GraphicsMagick++.lib"
                IMPORTED_IMPLIB_RELEASE "${releaseLibraryDirectory}/GraphicsMagick++.lib"
                IMPORTED_IMPLIB_RELWITHDEBINFO "${releaseLibraryDirectory}/GraphicsMagick++.lib"
                IMPORTED_IMPLIB_MINSIZEREL "${releaseLibraryDirectory}/GraphicsMagick++.lib"
                IMPORTED_LOCATION_DEBUG "${debugRuntimeDirectory}/GraphicsMagick++-12.dll"
                IMPORTED_LOCATION_RELEASE "${releaseRuntimeDirectory}/GraphicsMagick++-12.dll"
                IMPORTED_LOCATION_RELWITHDEBINFO "${releaseRuntimeDirectory}/GraphicsMagick++-12.dll"
                IMPORTED_LOCATION_MINSIZEREL "${releaseRuntimeDirectory}/GraphicsMagick++-12.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${includeDirectory}"
                INTERFACE_LINK_LIBRARIES FoundationGraphicsMagick
            )
        endif()

        get_property(explicitRuntimeTargets GLOBAL PROPERTY FOUNDATION_EXPLICIT_RUNTIME_TARGETS)
        list(APPEND explicitRuntimeTargets
            FoundationGraphicsMagick
            FoundationGraphicsMagickPP
        )
        set(runtimeNames
            "bz2"
            "freetype"
            "libpng16"
            "brotlicommon"
            "brotlidec"
            "jpeg62"
            "liblzma"
            "libsharpyuv"
            "libwebp"
            "libwebpmux"
            "tiff"
        )
        set(debugRuntimeNames
            "bz2d"
            "freetyped"
            "libpng16d"
            "brotlicommon"
            "brotlidec"
            "jpeg62"
            "liblzma"
            "libsharpyuv"
            "libwebp"
            "libwebpmux"
            "tiffd"
        )
        list(LENGTH runtimeNames runtimeCount)
        math(EXPR lastRuntimeIndex "${runtimeCount} - 1")
        foreach(runtimeIndex RANGE 0 ${lastRuntimeIndex})
            list(GET runtimeNames ${runtimeIndex} releaseRuntimeName)
            list(GET debugRuntimeNames ${runtimeIndex} debugRuntimeName)
            set(runtimeTarget "FoundationAgentImageRuntime${runtimeIndex}")
            if(NOT TARGET "${runtimeTarget}")
                add_library(${runtimeTarget} SHARED IMPORTED GLOBAL)
                set_target_properties(${runtimeTarget} PROPERTIES
                    IMPORTED_LOCATION_DEBUG
                        "${debugRuntimeDirectory}/${debugRuntimeName}.dll"
                    IMPORTED_LOCATION_RELEASE
                        "${releaseRuntimeDirectory}/${releaseRuntimeName}.dll"
                    IMPORTED_LOCATION_RELWITHDEBINFO
                        "${releaseRuntimeDirectory}/${releaseRuntimeName}.dll"
                    IMPORTED_LOCATION_MINSIZEREL
                        "${releaseRuntimeDirectory}/${releaseRuntimeName}.dll"
                )
            endif()
            list(APPEND explicitRuntimeTargets "${runtimeTarget}")
        endforeach()
        list(REMOVE_DUPLICATES explicitRuntimeTargets)
        set_property(
            GLOBAL
            PROPERTY FOUNDATION_EXPLICIT_RUNTIME_TARGETS
            "${explicitRuntimeTargets}"
        )
    else()
        find_package(PkgConfig REQUIRED)
        set(lunokiPreviousPkgConfigPath "$ENV{PKG_CONFIG_PATH}")
        set(ENV{PKG_CONFIG_PATH}
            "${vcpkgPrefix}/lib/pkgconfig:${vcpkgPrefix}/share/pkgconfig:${lunokiPreviousPkgConfigPath}"
        )
        pkg_check_modules(
            FOUNDATION_GRAPHICS_MAGICK_PP
            REQUIRED
            IMPORTED_TARGET
            GraphicsMagick++
        )
        set(ENV{PKG_CONFIG_PATH} "${lunokiPreviousPkgConfigPath}")
        if(NOT TARGET FoundationGraphicsMagickPP)
            add_library(FoundationGraphicsMagickPP INTERFACE IMPORTED GLOBAL)
            set_property(
                TARGET FoundationGraphicsMagickPP
                PROPERTY INTERFACE_LINK_LIBRARIES
                PkgConfig::FOUNDATION_GRAPHICS_MAGICK_PP
            )
            set_property(
                TARGET FoundationGraphicsMagickPP
                PROPERTY INTERFACE_LINK_DIRECTORIES
                "${vcpkgPrefix}/lib"
            )
        endif()
    endif()

    set(
        FOUNDATION_GRAPHICS_MAGICK_PP_TARGET
        FoundationGraphicsMagickPP
        PARENT_SCOPE
    )
endfunction()

function(FoundationDeployAgentImageRuntime targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR
            "Cannot deploy image runtime for missing target: ${targetName}"
        )
    endif()
    set(configDirectory
        "$<TARGET_FILE_DIR:${targetName}>/Tools/Image/GraphicsMagick/config"
    )
    add_custom_command(TARGET ${targetName} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${configDirectory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "$<IF:$<CONFIG:Debug>,${FOUNDATION_VCPKG_PREFIX}/debug/lib/GraphicsMagick-1.3.45/config,${FOUNDATION_VCPKG_PREFIX}/lib/GraphicsMagick-1.3.45/config>"
            "${configDirectory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${FOUNDATION_VCPKG_PREFIX}/share/graphicsmagick/GraphicsMagick-1.3.45/config"
            "${configDirectory}"
        COMMENT "Deploying the pinned FoundationAgent image runtime"
        VERBATIM
    )
endfunction()

function(FoundationResolveAgentMediaThirdParty)
    FoundationDetectVcpkgArchitecture(mediaArchitecture)
    if(WIN32)
        if(NOT "${mediaArchitecture}" STREQUAL "x64")
            message(FATAL_ERROR
                "FoundationAgent media preprocessing currently supports only "
                "Windows x64."
            )
        endif()
        set(mediaRoot "${FOUNDATION_DEPS_DIR}/MediaRuntime/FFmpeg")
        set(ffmpegArchiveName
            "ffmpeg-${FOUNDATION_AGENT_FFMPEG_BUILD}-win64-lgpl-shared.zip"
        )
        set(ffmpegArchivePath
            "${mediaRoot}/${FOUNDATION_AGENT_FFMPEG_RELEASE_TAG}/${ffmpegArchiveName}"
        )
        FoundationDownloadPinnedFile(
            "BtbN LGPL FFmpeg ${FOUNDATION_AGENT_FFMPEG_BUILD}"
            "https://github.com/BtbN/FFmpeg-Builds/releases/download/${FOUNDATION_AGENT_FFMPEG_RELEASE_TAG}/${ffmpegArchiveName}"
            "52d25fc4711078112ba622d07601f183371af43e2d93cbb6e5eab3e1c05387cb"
            "${ffmpegArchivePath}"
        )

        set(ffmpegExtractRoot
            "${mediaRoot}/${FOUNDATION_AGENT_FFMPEG_RELEASE_TAG}/Runtime"
        )
        file(GLOB_RECURSE ffmpegCandidates
            LIST_DIRECTORIES false
            "${ffmpegExtractRoot}/ffmpeg.exe"
        )
        list(LENGTH ffmpegCandidates ffmpegCandidateCount)
        if(ffmpegCandidateCount EQUAL 0)
            if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
                message(FATAL_ERROR
                    "The pinned FFmpeg runtime is not extracted: "
                    "${ffmpegExtractRoot}"
                )
            endif()
            file(MAKE_DIRECTORY "${ffmpegExtractRoot}")
            file(ARCHIVE_EXTRACT
                INPUT "${ffmpegArchivePath}"
                DESTINATION "${ffmpegExtractRoot}"
            )
            file(GLOB_RECURSE ffmpegCandidates
                LIST_DIRECTORIES false
                "${ffmpegExtractRoot}/ffmpeg.exe"
            )
            list(LENGTH ffmpegCandidates ffmpegCandidateCount)
        endif()
        if(NOT ffmpegCandidateCount EQUAL 1)
            message(FATAL_ERROR
                "Foundation could not resolve one ffmpeg.exe below: "
                "${ffmpegExtractRoot}"
            )
        endif()

        list(GET ffmpegCandidates 0 ffmpegExecutable)
        get_filename_component(ffmpegBinDirectory
            "${ffmpegExecutable}"
            DIRECTORY
        )
        if(NOT EXISTS "${ffmpegBinDirectory}/ffprobe.exe")
            message(FATAL_ERROR
                "The pinned FFmpeg runtime does not contain ffprobe.exe: "
                "${ffmpegBinDirectory}"
            )
        endif()
        set(ffprobeExecutable "${ffmpegBinDirectory}/ffprobe.exe")
    elseif(APPLE)
        find_program(ffmpegExecutable NAMES ffmpeg REQUIRED)
        find_program(ffprobeExecutable NAMES ffprobe REQUIRED)
        get_filename_component(ffmpegBinDirectory
            "${ffmpegExecutable}"
            DIRECTORY
        )
    else()
        message(FATAL_ERROR
            "FoundationAgent media preprocessing does not yet support "
            "'${CMAKE_SYSTEM_NAME}'."
        )
    endif()
    set(
        FOUNDATION_AGENT_MEDIA_BIN_DIRECTORY
        "${ffmpegBinDirectory}"
        CACHE INTERNAL
        "Pinned FoundationAgent FFmpeg binary directory."
        FORCE
    )
    set(
        FOUNDATION_AGENT_FFMPEG_EXECUTABLE
        "${ffmpegExecutable}"
        CACHE INTERNAL
        "Resolved FoundationAgent FFmpeg executable."
        FORCE
    )
    set(
        FOUNDATION_AGENT_FFPROBE_EXECUTABLE
        "${ffprobeExecutable}"
        CACHE INTERNAL
        "Resolved FoundationAgent FFprobe executable."
        FORCE
    )
endfunction()

function(FoundationDeployAgentMediaRuntime targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR
            "Cannot deploy media runtime for missing target: ${targetName}"
        )
    endif()
    if(NOT IS_DIRECTORY "${FOUNDATION_AGENT_MEDIA_BIN_DIRECTORY}")
        message(FATAL_ERROR
            "FoundationAgent media preprocessing dependencies have not been "
            "resolved."
        )
    endif()
    set(runtimeRoot
        "$<TARGET_FILE_DIR:${targetName}>/local_tools/ffmpeg"
    )
    if(WIN32)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${runtimeRoot}/bin"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${FOUNDATION_AGENT_MEDIA_BIN_DIRECTORY}"
                "${runtimeRoot}/bin"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FOUNDATION_ROOT}/Dependencies/Agent/Tools/Media/THIRD_PARTY_NOTICES.md"
                "${runtimeRoot}/THIRD_PARTY_NOTICES.md"
            COMMENT "Deploying the pinned FoundationAgent FFmpeg media runtime"
            VERBATIM
        )
    else()
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${runtimeRoot}/bin"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FOUNDATION_AGENT_FFMPEG_EXECUTABLE}"
                "${runtimeRoot}/bin/ffmpeg"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FOUNDATION_AGENT_FFPROBE_EXECUTABLE}"
                "${runtimeRoot}/bin/ffprobe"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FOUNDATION_ROOT}/Dependencies/Agent/Tools/Media/THIRD_PARTY_NOTICES.md"
                "${runtimeRoot}/THIRD_PARTY_NOTICES.md"
            COMMENT "Deploying the resolved FoundationAgent FFmpeg media runtime"
            VERBATIM
        )
    endif()
endfunction()

function(FoundationEnsureVcpkg outExecutable)
    if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        message(FATAL_ERROR "FoundationEnsureVcpkg cannot run when FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF.")
    endif()

    file(MAKE_DIRECTORY "${FOUNDATION_DEPS_DIR}")
    find_package(Git REQUIRED)

    if(NOT EXISTS "${FOUNDATION_VCPKG_ROOT}/.git")
        file(REMOVE_RECURSE "${FOUNDATION_VCPKG_ROOT}")
        FoundationRunThirdPartyCommand(
            "Creating the Foundation vcpkg checkout"
            "${GIT_EXECUTABLE}" init "${FOUNDATION_VCPKG_ROOT}"
        )
        FoundationRunThirdPartyCommand(
            "Registering the vcpkg repository"
            "${GIT_EXECUTABLE}" -C "${FOUNDATION_VCPKG_ROOT}"
            remote add origin https://github.com/microsoft/vcpkg.git
        )
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${FOUNDATION_VCPKG_ROOT}" rev-parse HEAD
        RESULT_VARIABLE revisionResult
        OUTPUT_VARIABLE currentRevision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT "${revisionResult}" STREQUAL "0"
        OR NOT "${currentRevision}" STREQUAL "${FOUNDATION_VCPKG_COMMIT}"
    )
        FoundationRunThirdPartyCommand(
            "Downloading the pinned vcpkg revision"
            "${GIT_EXECUTABLE}" -C "${FOUNDATION_VCPKG_ROOT}"
            fetch --depth 1 origin "${FOUNDATION_VCPKG_COMMIT}"
        )
        FoundationRunThirdPartyCommand(
            "Checking out the pinned vcpkg revision"
            "${GIT_EXECUTABLE}" -C "${FOUNDATION_VCPKG_ROOT}"
            checkout --detach "${FOUNDATION_VCPKG_COMMIT}"
        )
    endif()

    if(CMAKE_HOST_WIN32)
        set(vcpkgExecutable "${FOUNDATION_VCPKG_ROOT}/vcpkg.exe")
        if(NOT EXISTS "${vcpkgExecutable}")
            FoundationRunThirdPartyCommand(
                "Bootstrapping Foundation vcpkg"
                cmd /c "${FOUNDATION_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
            )
        endif()
    else()
        set(vcpkgExecutable "${FOUNDATION_VCPKG_ROOT}/vcpkg")
        if(NOT EXISTS "${vcpkgExecutable}")
            FoundationRunThirdPartyCommand(
                "Bootstrapping Foundation vcpkg"
                "${FOUNDATION_VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
            )
        endif()
    endif()

    if(NOT EXISTS "${vcpkgExecutable}")
        message(FATAL_ERROR "Foundation vcpkg executable was not created: ${vcpkgExecutable}")
    endif()

    set(${outExecutable} "${vcpkgExecutable}" PARENT_SCOPE)
endfunction()

function(FoundationInstallThirdParty)
    if(NOT ARGN)
        return()
    endif()
    if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        message(FATAL_ERROR
            "Third-party installation was requested while FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF."
        )
    endif()

    file(MAKE_DIRECTORY "${FOUNDATION_DEPS_DIR}")
    file(LOCK
        "${FOUNDATION_DEPS_DIR}/vcpkg.lock"
        GUARD FUNCTION
        TIMEOUT 1200
        RESULT_VARIABLE lockResult
    )
    if(NOT "${lockResult}" STREQUAL "0")
        message(FATAL_ERROR "Foundation could not acquire the vcpkg operation lock: ${lockResult}")
    endif()

    # The function-scoped lock deliberately covers checkout, bootstrap and the
    # complete install command. Concurrent solution configurations therefore
    # cannot mutate the same vcpkg tree or installed prefix at the same time.
    FoundationEnsureVcpkg(vcpkgExecutable)
    file(MAKE_DIRECTORY "${FOUNDATION_VCPKG_BINARY_CACHE}")

    set(vcpkgOverlayArguments
        --overlay-ports "${FOUNDATION_ROOT}/VcpkgOverlayPorts"
    )
    if(NOT "${FOUNDATION_VCPKG_OVERLAY_TRIPLETS}" STREQUAL "")
        list(APPEND vcpkgOverlayArguments
            --overlay-triplets "${FOUNDATION_VCPKG_OVERLAY_TRIPLETS}"
        )
    endif()

    if((CMAKE_HOST_WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND EXISTS "${FOUNDATION_VCPKG_ROOT}/ports/libmariadb/portfile.cmake")
        set(crossOverlayRoot
            "${FOUNDATION_DEPS_DIR}/VcpkgCrossOverlayPorts/${VCPKG_TARGET_TRIPLET}"
        )
        set(crossMariaDbPort "${crossOverlayRoot}/libmariadb")
        file(REMOVE_RECURSE "${crossMariaDbPort}")
        file(MAKE_DIRECTORY "${crossOverlayRoot}")
        file(COPY "${FOUNDATION_VCPKG_ROOT}/ports/libmariadb/"
            DESTINATION "${crossMariaDbPort}"
        )
        set(crossMariaDbPatch
            "lunoki-windows-host-linux-symlink.patch"
        )
        file(WRITE "${crossMariaDbPort}/${crossMariaDbPatch}" [=[diff --git a/cmake/symlink.cmake b/cmake/symlink.cmake
index 8e4f57d..a4ae321 100644
--- a/cmake/symlink.cmake
+++ b/cmake/symlink.cmake
@@ -9,10 +9,16 @@ MACRO(create_symlink symlink_name target install_path)
 # According to cmake documentation symlinks work on unix systems only
 IF(UNIX)
+  IF(CMAKE_HOST_WIN32)
+    SET(lunoki_link_command copy_if_different)
+  ELSE()
+    SET(lunoki_link_command create_symlink)
+  ENDIF()
+
   # Get target components 
   ADD_CUSTOM_COMMAND(
     OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${symlink_name}
     COMMAND ${CMAKE_COMMAND} ARGS -E remove -f ${symlink_name}
-    COMMAND ${CMAKE_COMMAND} ARGS -E create_symlink $<TARGET_FILE_NAME:${target}> ${symlink_name}
+    COMMAND ${CMAKE_COMMAND} ARGS -E ${lunoki_link_command} $<TARGET_FILE_NAME:${target}> ${symlink_name}
     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
     DEPENDS ${target}
     )
]=])
        set(crossMariaDbPortFile "${crossMariaDbPort}/portfile.cmake")
        file(READ "${crossMariaDbPortFile}" crossMariaDbPortContent)
        if(NOT crossMariaDbPortContent MATCHES "${crossMariaDbPatch}")
            string(REPLACE
                "        no-abs-path.diff"
                "        no-abs-path.diff\n        ${crossMariaDbPatch}"
                crossMariaDbPortContent
                "${crossMariaDbPortContent}"
            )
            file(WRITE
                "${crossMariaDbPortFile}"
                "${crossMariaDbPortContent}"
            )
        endif()

        set(crossLibWebSocketsPort "${crossOverlayRoot}/libwebsockets")
        file(REMOVE_RECURSE "${crossLibWebSocketsPort}")
        file(COPY "${FOUNDATION_VCPKG_ROOT}/ports/libwebsockets/"
            DESTINATION "${crossLibWebSocketsPort}"
        )
        set(crossLibWebSocketsPatch
            "lunoki-openssl3-rsa-pss.patch"
        )
        file(WRITE "${crossLibWebSocketsPort}/${crossLibWebSocketsPatch}" [=[diff --git a/lib/tls/openssl/lws-genrsa.c b/lib/tls/openssl/lws-genrsa.c
index 6ad8c7c..5c9b30f 100644
--- a/lib/tls/openssl/lws-genrsa.c
+++ b/lib/tls/openssl/lws-genrsa.c
@@ -302,7 +302,8 @@ lws_genrsa_public_verify(struct lws_genrsa_ctx *ctx, enum lws_genhash_types hash
 		if (!md)
 			return -1;
 
-#if defined(LWS_HAVE_RSA_verify_pss_mgf1)
+#if defined(LWS_HAVE_RSA_verify_pss_mgf1) && \
+	(!defined(OPENSSL_VERSION_MAJOR) || OPENSSL_VERSION_MAJOR < 3)
 		n = RSA_verify_pss_mgf1(ctx->rsa, in, SSL_SIZE_CAST(h), md, NULL, -1,
 					(uint8_t *)sig,
 #else
]=])
        set(crossLibWebSocketsPortFile
            "${crossLibWebSocketsPort}/portfile.cmake"
        )
        file(READ
            "${crossLibWebSocketsPortFile}"
            crossLibWebSocketsPortContent
        )
        if(NOT crossLibWebSocketsPortContent MATCHES
            "${crossLibWebSocketsPatch}")
            string(REPLACE
                "        export-include-path.patch"
                "        export-include-path.patch\n        ${crossLibWebSocketsPatch}"
                crossLibWebSocketsPortContent
                "${crossLibWebSocketsPortContent}"
            )
            file(WRITE
                "${crossLibWebSocketsPortFile}"
                "${crossLibWebSocketsPortContent}"
            )
        endif()

        # libssh2's configure-time symbol probe links against Zig's complete
        # libc shim and can therefore report explicit_bzero even when the
        # selected glibc baseline (2.17) does not export it at runtime. Pin the
        # result off for this cross triplet so libssh2 uses its portable
        # volatile-memory fallback instead of producing a binary with a newer
        # glibc requirement.
        set(crossLibSsh2Port "${crossOverlayRoot}/libssh2")
        file(REMOVE_RECURSE "${crossLibSsh2Port}")
        file(COPY "${FOUNDATION_VCPKG_ROOT}/ports/libssh2/"
            DESTINATION "${crossLibSsh2Port}"
        )
        set(crossLibSsh2PortFile "${crossLibSsh2Port}/portfile.cmake")
        file(READ "${crossLibSsh2PortFile}" crossLibSsh2PortContent)
        if(NOT crossLibSsh2PortContent MATCHES
            "HAVE_EXPLICIT_BZERO:INTERNAL=0")
            string(REPLACE
                "        -DENABLE_DEBUG_LOGGING=OFF"
                "        -DENABLE_DEBUG_LOGGING=OFF\n        -DHAVE_EXPLICIT_BZERO:INTERNAL=0"
                crossLibSsh2PortContent
                "${crossLibSsh2PortContent}"
            )
            file(WRITE
                "${crossLibSsh2PortFile}"
                "${crossLibSsh2PortContent}"
            )
        endif()
        list(PREPEND vcpkgOverlayArguments
            --overlay-ports "${crossOverlayRoot}"
        )
    endif()

    set(vcpkgBuildEnvironment
        "VCPKG_DEFAULT_BINARY_CACHE=${FOUNDATION_VCPKG_BINARY_CACHE}"
    )
    if(APPLE)
        # Apple Clang treats /usr/local/include as an implicit system search
        # directory.  A developer-installed package there can otherwise win
        # over vcpkg's `-isystem` include directory while a port is compiling,
        # mixing headers from one ABI/version with Foundation's pinned libraries.
        # Keep solution-owned headers ahead of implicit system directories for
        # every vcpkg build launched by Foundation.
        set(lunokiVcpkgPrivateInclude
            "${FOUNDATION_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        )
        set(lunokiVcpkgCFlags "$ENV{CFLAGS}")
        set(lunokiVcpkgCxxFlags "$ENV{CXXFLAGS}")
        string(APPEND lunokiVcpkgCFlags " -I${lunokiVcpkgPrivateInclude}")
        string(APPEND lunokiVcpkgCxxFlags " -I${lunokiVcpkgPrivateInclude}")
        string(STRIP "${lunokiVcpkgCFlags}" lunokiVcpkgCFlags)
        string(STRIP "${lunokiVcpkgCxxFlags}" lunokiVcpkgCxxFlags)
        list(APPEND vcpkgBuildEnvironment
            "CFLAGS=${lunokiVcpkgCFlags}"
            "CXXFLAGS=${lunokiVcpkgCxxFlags}"
        )
    endif()

    FoundationRunThirdPartyCommand(
        "Installing Foundation third-party packages: ${ARGN}"
        "${CMAKE_COMMAND}" -E env
        ${vcpkgBuildEnvironment}
        "${vcpkgExecutable}" install ${ARGN}
        --triplet "${VCPKG_TARGET_TRIPLET}"
        ${vcpkgOverlayArguments}
    )
endfunction()

# Installs a package set into an explicit, solution-owned triplet without
# changing the triplet used by the rest of the current CMake configure. This
# is primarily used by plugins whose linkage model intentionally differs from
# Foundation's normal static third-party dependency policy.
function(FoundationInstallThirdPartyForTriplet targetTriplet overlayTriplets)
    if("${targetTriplet}" STREQUAL "")
        message(FATAL_ERROR
            "FoundationInstallThirdPartyForTriplet requires a target triplet."
        )
    endif()
    if(NOT ARGN)
        return()
    endif()

    set(VCPKG_TARGET_TRIPLET "${targetTriplet}")
    set(FOUNDATION_VCPKG_OVERLAY_TRIPLETS "${overlayTriplets}")
    FoundationInstallThirdParty(${ARGN})
endfunction()

macro(FoundationResolveQt)
    set(multiValueArguments COMPONENTS)
    cmake_parse_arguments(FOUNDATION_QT "" "" "${multiValueArguments}" ${ARGN})

    if(NOT FOUNDATION_QT_COMPONENTS)
        message(FATAL_ERROR "FoundationResolveQt requires at least one Qt component.")
    endif()

    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${FOUNDATION_QT_COMPONENTS})
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${FOUNDATION_QT_COMPONENTS})
endmacro()

function(FoundationResolveFoundationThirdParty)
    set(vcpkgPrefix "${FOUNDATION_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if(FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        get_property(dependenciesInstalled GLOBAL PROPERTY FOUNDATION_FOUNDATION_DEPS_INSTALLED)
        if(NOT dependenciesInstalled)
            FoundationInstallThirdParty(
                libssh2
                curl
                libmariadb
                hiredis
                libwebsockets
                openssl
                zlib
                boost-container
                boost-interprocess
            )
            set_property(GLOBAL PROPERTY FOUNDATION_FOUNDATION_DEPS_INSTALLED TRUE)
        endif()
    elseif(NOT IS_DIRECTORY "${vcpkgPrefix}")
        message(FATAL_ERROR
            "FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF and the preinstalled dependency prefix is missing: "
            "${vcpkgPrefix}. Populate it externally or set FOUNDATION_VCPKG_ROOT/VCPKG_TARGET_TRIPLET."
        )
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${vcpkgPrefix}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    set(
        FOUNDATION_VCPKG_PREFIX
        "${vcpkgPrefix}"
        CACHE INTERNAL
        "Foundation vcpkg prefix."
        FORCE
    )

    # find_package creates directory-scoped imported targets. This resolver is
    # intentionally executed for every consuming directory; only installation
    # is globally de-duplicated. Sibling projects therefore receive valid local
    # package targets instead of inheriting unusable names from another sibling.
    foreach(cacheVariable
        CURL_DIR
        LIB_EAY_DEBUG
        LIB_EAY_RELEASE
        OPENSSL_APPLINK_SOURCE
        OPENSSL_INCLUDE_DIR
        OPENSSL_ROOT_DIR
        OpenSSL_DIR
        SSL_EAY_DEBUG
        SSL_EAY_RELEASE
        ZLIB_INCLUDE_DIR
        ZLIB_LIBRARY_DEBUG
        ZLIB_LIBRARY_RELEASE
        ZLIB_ROOT
        libssh2_DIR
        unofficial-libmariadb_DIR
        hiredis_DIR
        libwebsockets_DIR
        Boost_DIR
    )
        unset(${cacheVariable} CACHE)
    endforeach()

    find_package(OpenSSL CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(libssh2 CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(CURL CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(unofficial-libmariadb CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(hiredis CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(libwebsockets CONFIG REQUIRED PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)
    find_package(Boost CONFIG REQUIRED COMPONENTS container interprocess PATHS "${vcpkgPrefix}" NO_DEFAULT_PATH)

    # Xcode and Visual Studio generate RelWithDebInfo and MinSizeRel by default.
    # Depending on the vcpkg OpenSSL config path, the imported targets expose
    # either a Release location or only a configuration-less location. The
    # directory-level Release mapping is invalid for the latter and makes CMake
    # ignore an otherwise usable IMPORTED_LOCATION, so choose the mapping from
    # the actual target properties.
    foreach(openSslTarget IN ITEMS OpenSSL::SSL OpenSSL::Crypto)
        if(TARGET "${openSslTarget}")
            get_target_property(
                openSslReleaseLocation
                "${openSslTarget}"
                IMPORTED_LOCATION_RELEASE
            )
            if(openSslReleaseLocation)
                set_target_properties("${openSslTarget}" PROPERTIES
                    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
                    MAP_IMPORTED_CONFIG_MINSIZEREL Release
                )
            else()
                get_target_property(
                    openSslGenericLocation
                    "${openSslTarget}"
                    IMPORTED_LOCATION
                )
                if(NOT openSslGenericLocation)
                    message(FATAL_ERROR
                        "${openSslTarget} has neither a Release nor a "
                        "configuration-less imported location."
                    )
                endif()
                set_target_properties("${openSslTarget}" PROPERTIES
                    MAP_IMPORTED_CONFIG_RELWITHDEBINFO ""
                    MAP_IMPORTED_CONFIG_MINSIZEREL ""
                )
            endif()
        endif()
    endforeach()

    if(TARGET libssh2::libssh2)
        set(libSsh2Target libssh2::libssh2)
    elseif(TARGET Libssh2::libssh2)
        set(libSsh2Target Libssh2::libssh2)
    else()
        message(FATAL_ERROR "Foundation could not resolve the libssh2 target.")
    endif()

    if(TARGET CURL::libcurl)
        set(curlTarget CURL::libcurl)
    else()
        message(FATAL_ERROR "Foundation could not resolve the libcurl target.")
    endif()

    if(TARGET unofficial::libmariadb)
        set(mariaDbTarget unofficial::libmariadb)
    else()
        message(FATAL_ERROR "Foundation could not resolve the MariaDB Connector/C target.")
    endif()

    if(TARGET hiredis::hiredis)
        set(hiredisTarget hiredis::hiredis)
    elseif(TARGET hiredis)
        set(hiredisTarget hiredis)
    else()
        message(FATAL_ERROR "Foundation could not resolve the hiredis target.")
    endif()

    if(TARGET websockets_shared)
        set(webSocketsTarget websockets_shared)
    elseif(TARGET websockets)
        set(webSocketsTarget websockets)
    elseif(TARGET libwebsockets::websockets)
        set(webSocketsTarget libwebsockets::websockets)
    else()
        message(FATAL_ERROR "Foundation could not resolve the libwebsockets target.")
    endif()

    if(NOT TARGET OpenSSL::SSL OR NOT TARGET OpenSSL::Crypto)
        message(FATAL_ERROR "Foundation could not resolve the OpenSSL runtime targets.")
    endif()
    if(NOT TARGET Boost::container OR NOT TARGET Boost::interprocess)
        message(FATAL_ERROR "Foundation could not resolve the required Boost targets.")
    endif()

    get_property(explicitRuntimeTargets GLOBAL PROPERTY FOUNDATION_EXPLICIT_RUNTIME_TARGETS)

    if(WIN32)
        set(debugZlibRuntime "${vcpkgPrefix}/debug/bin/zd.dll")
        set(releaseZlibRuntime "${vcpkgPrefix}/bin/z.dll")
        set(debugZlibLibrary "${vcpkgPrefix}/debug/lib/zd.lib")
        set(releaseZlibLibrary "${vcpkgPrefix}/lib/z.lib")
        if(NOT EXISTS "${debugZlibRuntime}"
            OR NOT EXISTS "${releaseZlibRuntime}"
            OR NOT EXISTS "${debugZlibLibrary}"
            OR NOT EXISTS "${releaseZlibLibrary}")
            message(FATAL_ERROR "Foundation could not find the zlib runtime DLLs.")
        endif()

        set(zlibRuntimeTarget FoundationZlibRuntime)
        if(NOT TARGET "${zlibRuntimeTarget}")
            add_library(${zlibRuntimeTarget} SHARED IMPORTED GLOBAL)
            set_target_properties(${zlibRuntimeTarget} PROPERTIES
                IMPORTED_LOCATION_DEBUG "${debugZlibRuntime}"
                IMPORTED_LOCATION_RELEASE "${releaseZlibRuntime}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${releaseZlibRuntime}"
                IMPORTED_LOCATION_MINSIZEREL "${releaseZlibRuntime}"
                IMPORTED_IMPLIB_DEBUG "${debugZlibLibrary}"
                IMPORTED_IMPLIB_RELEASE "${releaseZlibLibrary}"
                IMPORTED_IMPLIB_RELWITHDEBINFO "${releaseZlibLibrary}"
                IMPORTED_IMPLIB_MINSIZEREL "${releaseZlibLibrary}"
                INTERFACE_INCLUDE_DIRECTORIES "${vcpkgPrefix}/include"
            )
        endif()
        list(APPEND explicitRuntimeTargets "${zlibRuntimeTarget}")

        set(debugLibUvRuntime "${vcpkgPrefix}/debug/bin/uv.dll")
        set(releaseLibUvRuntime "${vcpkgPrefix}/bin/uv.dll")
        if(NOT EXISTS "${debugLibUvRuntime}" OR NOT EXISTS "${releaseLibUvRuntime}")
            message(FATAL_ERROR "Foundation could not find the libuv runtime DLLs required by libwebsockets.")
        endif()

        set(libUvRuntimeTarget FoundationLibUvRuntime)
        if(NOT TARGET "${libUvRuntimeTarget}")
            add_library(${libUvRuntimeTarget} SHARED IMPORTED GLOBAL)
            set_target_properties(${libUvRuntimeTarget} PROPERTIES
                IMPORTED_LOCATION_DEBUG "${debugLibUvRuntime}"
                IMPORTED_LOCATION_RELEASE "${releaseLibUvRuntime}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${releaseLibUvRuntime}"
                IMPORTED_LOCATION_MINSIZEREL "${releaseLibUvRuntime}"
            )
        endif()
        list(APPEND explicitRuntimeTargets "${libUvRuntimeTarget}")

        foreach(runtimeLibrary IN ITEMS ssl crypto)
            file(GLOB debugRuntimeFiles "${vcpkgPrefix}/debug/bin/lib${runtimeLibrary}-*.dll")
            file(GLOB releaseRuntimeFiles "${vcpkgPrefix}/bin/lib${runtimeLibrary}-*.dll")
            list(SORT debugRuntimeFiles)
            list(SORT releaseRuntimeFiles)
            list(LENGTH debugRuntimeFiles debugRuntimeFileCount)
            list(LENGTH releaseRuntimeFiles releaseRuntimeFileCount)
            if(debugRuntimeFileCount EQUAL 0 OR releaseRuntimeFileCount EQUAL 0)
                message(FATAL_ERROR "Foundation could not find the OpenSSL ${runtimeLibrary} runtime DLLs.")
            endif()

            list(GET debugRuntimeFiles -1 debugRuntimeFile)
            list(GET releaseRuntimeFiles -1 releaseRuntimeFile)
            set(runtimeTarget "FoundationOpenSsl${runtimeLibrary}Runtime")
            if(NOT TARGET "${runtimeTarget}")
                add_library(${runtimeTarget} SHARED IMPORTED GLOBAL)
                set_target_properties(${runtimeTarget} PROPERTIES
                    IMPORTED_LOCATION_DEBUG "${debugRuntimeFile}"
                    IMPORTED_LOCATION_RELEASE "${releaseRuntimeFile}"
                    IMPORTED_LOCATION_RELWITHDEBINFO "${releaseRuntimeFile}"
                    IMPORTED_LOCATION_MINSIZEREL "${releaseRuntimeFile}"
                )
            endif()
            list(APPEND explicitRuntimeTargets "${runtimeTarget}")
        endforeach()
    endif()

    list(REMOVE_DUPLICATES explicitRuntimeTargets)
    set_property(GLOBAL PROPERTY FOUNDATION_EXPLICIT_RUNTIME_TARGETS "${explicitRuntimeTargets}")

    set(FOUNDATION_LIBSSH2_TARGET "${libSsh2Target}" PARENT_SCOPE)
    set(FOUNDATION_CURL_TARGET "${curlTarget}" PARENT_SCOPE)
    set(FOUNDATION_MARIADB_TARGET "${mariaDbTarget}" PARENT_SCOPE)
    set(FOUNDATION_HIREDIS_TARGET "${hiredisTarget}" PARENT_SCOPE)
    set(FOUNDATION_LIBWEBSOCKETS_TARGET "${webSocketsTarget}" PARENT_SCOPE)
    set(FOUNDATION_OPENSSL_TARGETS OpenSSL::SSL OpenSSL::Crypto PARENT_SCOPE)
    set(FOUNDATION_BOOST_TARGETS Boost::container Boost::interprocess PARENT_SCOPE)
endfunction()

function(FoundationResolveClientRagThirdParty)
    set(vcpkgPrefix "${FOUNDATION_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    FoundationDetectVcpkgArchitecture(ragArchitecture)

    set(useOfficialOnnxRuntime FALSE)
    if(APPLE AND ragArchitecture STREQUAL "arm64")
        set(useOfficialOnnxRuntime TRUE)
    endif()

    if(FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        get_property(sqliteInstalled GLOBAL PROPERTY FOUNDATION_CLIENT_RAG_SQLITE_INSTALLED)
        if(NOT sqliteInstalled)
            FoundationInstallThirdParty("sqlite3[fts5]")
            set_property(GLOBAL PROPERTY FOUNDATION_CLIENT_RAG_SQLITE_INSTALLED TRUE)
        endif()
        get_property(semanticInstalled GLOBAL PROPERTY
            FOUNDATION_CLIENT_RAG_SEMANTIC_INSTALLED)
        if(NOT semanticInstalled)
            if(useOfficialOnnxRuntime)
                FoundationInstallThirdParty(hnswlib)
            else()
                FoundationInstallThirdParty(onnxruntime hnswlib)
            endif()
            set_property(GLOBAL PROPERTY
                FOUNDATION_CLIENT_RAG_SEMANTIC_INSTALLED TRUE)
        endif()
    endif()

    find_package(
        unofficial-sqlite3 CONFIG REQUIRED
        PATHS "${vcpkgPrefix}"
        NO_DEFAULT_PATH
    )
    if(NOT TARGET unofficial::sqlite3::sqlite3)
        message(FATAL_ERROR "Foundation Client RAG could not resolve SQLite3.")
    endif()
    find_package(
        hnswlib CONFIG REQUIRED
        PATHS "${vcpkgPrefix}"
        NO_DEFAULT_PATH
    )
    if(useOfficialOnnxRuntime)
        set(onnxRuntimeVersion "1.23.2")
        set(onnxRuntimeArchiveRoot
            "${FOUNDATION_DEPS_DIR}/ONNXRuntime/v${onnxRuntimeVersion}"
        )
        set(onnxRuntimeArchive
            "${onnxRuntimeArchiveRoot}/onnxruntime-osx-arm64-${onnxRuntimeVersion}.tgz"
        )
        FoundationDownloadPinnedFile(
            "Official ONNX Runtime macOS arm64 package"
            "https://github.com/microsoft/onnxruntime/releases/download/v${onnxRuntimeVersion}/onnxruntime-osx-arm64-${onnxRuntimeVersion}.tgz"
            "b4d513ab2b26f088c66891dbbc1408166708773d7cc4163de7bdca0e9bbb7856"
            "${onnxRuntimeArchive}"
        )
        set(onnxRuntimeRoot
            "${onnxRuntimeArchiveRoot}/onnxruntime-osx-arm64-${onnxRuntimeVersion}"
        )
        set(onnxRuntimeHeader
            "${onnxRuntimeRoot}/include/onnxruntime_cxx_api.h"
        )
        set(onnxRuntimeLibrary
            "${onnxRuntimeRoot}/lib/libonnxruntime.${onnxRuntimeVersion}.dylib"
        )
        if(NOT EXISTS "${onnxRuntimeHeader}"
            OR NOT EXISTS "${onnxRuntimeLibrary}")
            if(NOT FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
                message(FATAL_ERROR
                    "The pinned ONNX Runtime package is not extracted while "
                    "FOUNDATION_AUTO_INSTALL_THIRD_PARTY=OFF: ${onnxRuntimeRoot}"
                )
            endif()
            file(ARCHIVE_EXTRACT
                INPUT "${onnxRuntimeArchive}"
                DESTINATION "${onnxRuntimeArchiveRoot}"
            )
        endif()
        if(NOT EXISTS "${onnxRuntimeHeader}"
            OR NOT EXISTS "${onnxRuntimeLibrary}")
            message(FATAL_ERROR
                "The pinned ONNX Runtime package has an incompatible layout."
            )
        endif()
        if(NOT TARGET FoundationOnnxRuntime)
            add_library(FoundationOnnxRuntime SHARED IMPORTED GLOBAL)
            set_target_properties(FoundationOnnxRuntime PROPERTIES
                IMPORTED_LOCATION "${onnxRuntimeLibrary}"
                INTERFACE_INCLUDE_DIRECTORIES "${onnxRuntimeRoot}/include"
            )
        endif()
        set(onnxRuntimeTarget FoundationOnnxRuntime)
    else()
        find_package(
            onnxruntime CONFIG REQUIRED
            PATHS "${vcpkgPrefix}"
            NO_DEFAULT_PATH
        )
        if(TARGET onnxruntime::onnxruntime)
            set(onnxRuntimeTarget onnxruntime::onnxruntime)
        elseif(TARGET onnxruntime)
            set(onnxRuntimeTarget onnxruntime)
        else()
            message(FATAL_ERROR
                "Foundation Client RAG could not resolve ONNX Runtime.")
        endif()
    endif()
    if(NOT TARGET hnswlib::hnswlib)
        message(FATAL_ERROR "Foundation Client RAG could not resolve hnswlib.")
    endif()

    set(embeddingRoot
        "${FOUNDATION_DEPS_DIR}/Models/bge-small-zh-v1.5/75c43b0"
    )
    FoundationDownloadPinnedFile(
        "BGE small zh v1.5 quantized ONNX model"
        "https://huggingface.co/Xenova/bge-small-zh-v1.5/resolve/75c43b0/onnx/model_quantized.onnx?download=true"
        "15b717c382bcb518ba457b93ea6850ede7f4f1cd8937454aa06972366cd19bcc"
        "${embeddingRoot}/model_quantized.onnx"
    )
    FoundationDownloadPinnedFile(
        "BGE small zh v1.5 WordPiece vocabulary"
        "https://huggingface.co/Xenova/bge-small-zh-v1.5/resolve/75c43b0/vocab.txt?download=true"
        "45bbac6b341c319adc98a532532882e91a9cefc0329aa57bac9ae761c27b291c"
        "${embeddingRoot}/vocab.txt"
    )
    FoundationDownloadPinnedFile(
        "BGE small zh v1.5 model configuration"
        "https://huggingface.co/Xenova/bge-small-zh-v1.5/resolve/75c43b0/config.json?download=true"
        "d4193ead3a810fd694fa8a31d7fc72fbaebc0668b603e398734bf2f6538ff42f"
        "${embeddingRoot}/config.json"
    )
    set(
        FOUNDATION_CLIENT_SQLITE_TARGET
        unofficial::sqlite3::sqlite3
        PARENT_SCOPE
    )
    set(FOUNDATION_CLIENT_ONNXRUNTIME_TARGET
        "${onnxRuntimeTarget}" PARENT_SCOPE)
    set(FOUNDATION_CLIENT_ONNXRUNTIME_LIBRARY
        "${onnxRuntimeLibrary}" PARENT_SCOPE)
    set(FOUNDATION_CLIENT_HNSWLIB_TARGET
        hnswlib::hnswlib PARENT_SCOPE)
    set(FOUNDATION_CLIENT_EMBEDDING_ASSET_DIRECTORY
        "${embeddingRoot}" PARENT_SCOPE)
endfunction()

function(FoundationResolveServerThirdParty)
    set(vcpkgPrefix "${FOUNDATION_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if(FOUNDATION_AUTO_INSTALL_THIRD_PARTY)
        get_property(protobufInstalled GLOBAL PROPERTY FOUNDATION_SERVER_PROTOBUF_INSTALLED)
        if(NOT protobufInstalled)
            FoundationInstallThirdParty(protobuf)
            set_property(GLOBAL PROPERTY FOUNDATION_SERVER_PROTOBUF_INSTALLED TRUE)
        endif()
    endif()

    unset(absl_DIR CACHE)
    unset(utf8_range_DIR CACHE)
    unset(Protobuf_DIR CACHE)
    unset(protobuf_DIR CACHE)
    find_package(
        absl CONFIG REQUIRED
        PATHS "${vcpkgPrefix}/share/absl"
        NO_DEFAULT_PATH
    )
    find_package(
        utf8_range CONFIG REQUIRED
        PATHS "${vcpkgPrefix}/share/utf8_range"
        NO_DEFAULT_PATH
    )
    find_package(
        Protobuf CONFIG REQUIRED
        PATHS "${vcpkgPrefix}"
        NO_DEFAULT_PATH
    )
    if(NOT TARGET protobuf::libprotobuf)
        message(FATAL_ERROR "Foundation Server could not resolve protobuf::libprotobuf.")
    endif()
    set(
        FOUNDATION_SERVER_PROTOBUF_TARGET
        protobuf::libprotobuf
        PARENT_SCOPE
    )
endfunction()

function(FoundationDeployRuntimeDependencies targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "Cannot deploy runtime dependencies for missing target: ${targetName}")
    endif()

    get_target_property(
        runtimeDeploymentConfigured
        ${targetName}
        FOUNDATION_RUNTIME_DEPLOYMENT_CONFIGURED
    )
    if(runtimeDeploymentConfigured)
        return()
    endif()
    set_property(
        TARGET ${targetName}
        PROPERTY FOUNDATION_RUNTIME_DEPLOYMENT_CONFIGURED TRUE
    )

    if(WIN32)
        get_property(
            msvcSystemRuntimeLibraries
            GLOBAL
            PROPERTY FOUNDATION_MSVC_SYSTEM_RUNTIME_LIBS
        )
        if(MSVC AND NOT msvcSystemRuntimeLibraries)
            message(FATAL_ERROR
                "MSVC runtime deployment is unavailable for target: "
                "${targetName}"
            )
        endif()
        set(msvcSystemRuntimeNames)
        foreach(runtimeLibrary IN LISTS msvcSystemRuntimeLibraries)
            get_filename_component(runtimeName "${runtimeLibrary}" NAME)
            list(APPEND msvcSystemRuntimeNames "${runtimeName}")
        endforeach()
        list(JOIN msvcSystemRuntimeNames "|" msvcSystemRuntimeNamesArgument)

        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DFOUNDATION_RUNTIME_FILES=$<JOIN:$<TARGET_RUNTIME_DLLS:${targetName}>,|>"
                "-DFOUNDATION_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:${targetName}>"
                -P
                "${FOUNDATION_ROOT}/CMake/FoundationCopyRuntimeDependencies.cmake"
            VERBATIM
        )

        get_property(explicitRuntimeTargets GLOBAL PROPERTY FOUNDATION_EXPLICIT_RUNTIME_TARGETS)
        foreach(runtimeTarget IN LISTS explicitRuntimeTargets)
            if(TARGET "${runtimeTarget}")
                add_custom_command(TARGET ${targetName} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${runtimeTarget}>
                        $<TARGET_FILE_DIR:${targetName}>
                    VERBATIM
                )
            endif()
        endforeach()

        if(MSVC)
            add_custom_command(TARGET ${targetName} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${msvcSystemRuntimeLibraries}
                    $<TARGET_FILE_DIR:${targetName}>
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        endif()

        get_target_property(runtimeTargetType ${targetName} TYPE)
        if("${runtimeTargetType}" STREQUAL "EXECUTABLE")
            add_custom_command(TARGET ${targetName} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    "-DFOUNDATION_RUNTIME_BINARY=$<TARGET_FILE:${targetName}>"
                    "-DFOUNDATION_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:${targetName}>"
                    "-DFOUNDATION_REQUIRED_APP_LOCAL_RUNTIME_FILES=${msvcSystemRuntimeNamesArgument}"
                    -P
                    "${FOUNDATION_ROOT}/CMake/FoundationValidateRuntimeDependencies.cmake"
                COMMENT
                    "Validating runtime dependencies for ${targetName}"
                VERBATIM
            )
        endif()
    endif()
endfunction()
