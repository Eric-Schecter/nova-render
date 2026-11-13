# Pin upstream v2.9.7 commit, download immutable files and verify both hashes.
vcpkg_download_distfile(HEADER
    URLS "https://raw.githubusercontent.com/syoyo/tinygltf/488a70a3df62a4df1a736e9e56fb8836580c4888/tiny_gltf.h"
    FILENAME "nova-tinygltf-488a70a3-tiny_gltf.h"
    SHA512 652c0207bd77679a9df7a050692b079762b9175c99dd1da14da5d1b153b637d2aeb165b8aa1ae4681e1efcd1b794f36646641bf9b9bf16a3701d7db27d427fa0
)
vcpkg_download_distfile(LICENSE_FILE
    URLS "https://raw.githubusercontent.com/syoyo/tinygltf/488a70a3df62a4df1a736e9e56fb8836580c4888/LICENSE"
    FILENAME "nova-tinygltf-488a70a3-LICENSE"
    SHA512 bf3e8fea5e178f1268a095c98732b20a51f0d75c217dcdf70a50d8c6aac3f2e4f145aa2dcab720ebe4ba0ba00c1a50d9937fef8b5f13b85fb82479cc72696fcd
)
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
file(READ "${HEADER}" content)
string(REPLACE "#include \"json.hpp\"" "#include <nlohmann/json.hpp>" content "${content}")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/tiny_gltf.h" "${content}")
vcpkg_install_copyright(FILE_LIST "${LICENSE_FILE}")
