# CPM Package Lock
# This file should be committed to version control

# SDL (unversioned)
# CPMDeclarePackage(SDL
#  GIT_TAG release-2.0.22
#  GITHUB_REPOSITORY libsdl-org/SDL
#  OPTIONS
#    "SDL_SHARED OFF"
#    "SDL_STATIC ON"
#)
# imgui (unversioned)
# CPMDeclarePackage(imgui
#  GIT_TAG docking
#  DOWNLOAD_ONLY YES
#  GITHUB_REPOSITORY ocornut/imgui
#)
# spdlog
CPMDeclarePackage(spdlog
  VERSION 1.10.0
  GITHUB_REPOSITORY gabime/spdlog
  OPTIONS
    "SPDLOG_NO_EXCEPTIONS OFF"
)
