# - Try to find libOAuth
# Once done, this will define
#
#  OAUTH_FOUND - system has Oauth
#  OAUTH_INCLUDE_DIRS - the OAuth include directories
#  OAUTH_LIBRARIES - link these to use OAuth

# INCLUDE(UsePkgConfig)

# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
# PKGCONFIG(oauth _oauthIncDir _oauthLinkDir _flickculrLinkFlags _oauthCflags)

# SET(OAUTH_LIBS ${_oauthCflags})

FIND_PATH(OAUTH_INCLUDE_DIR oauth.h
  PATHS /usr/include
  /usr/local/include
  /opt/local/include
  HINTS ENV OAUTH_INCLUDE_DIR
  PATH_SUFFIXES oauth
)

FIND_LIBRARY(OAUTH_LIBRARY
  NAMES ${OAUTH_NAMES} liboauth.so liboauth.dylib
  PATHS /usr/lib /usr/local/lib /opt/local/lib
  HINTS ENV OAUTH_LIBRARY
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OAUTH DEFAULT_MSG OAUTH_LIBRARY OAUTH_INCLUDE_DIR)

IF(OAUTH_FOUND)
  SET(OAUTH_LIBRARIES ${OAUTH_LIBRARY})
  SET(OAUTH_INCLUDE_DIRS ${OAUTH_INCLUDE_DIR})
ENDIF(OAUTH_FOUND)