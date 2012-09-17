/*
    This file is part of darktable,
    copyright (c) 2012 Pierre Lamot

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <glib.h>

/** oauth context */
struct dt_oauth_ctx_priv_t;
typedef struct dt_oauth_ctx_t
{
  char *endpoint;
  char *alternative_endpoint;

  char *consumer_key;
  char *consumer_secret;

  char *token_key;
  char *token_secret;

  gboolean use_authorize_header;

  struct dt_oauth_ctx_priv_t *priv;
} dt_oauth_ctx_t;
/**
 * Error codes returned by oauth functions
 *
 * codes 0 means success
 * codes from 1 to 499 are CURL errors
 * codes from 500 to 999 are oauth specific errors
 * codes above might be used by user in callback functions
 */
typedef enum dt_oauth_errors_t
{
  ///everything went fine
  DT_OAUTH_OK = 0,
  /// the context hasn't been provided or is in an invalid state
  DT_OAUTH_NO_VALID_CONTEXT = 500,
  /// the given option doesn't exist or is unavailable
  DT_OAUTH_INVALID_OPTION,
  /// error while parsing oauth response, the token field is unavailable
  DT_OAUTH_PARSE_ERROR_TOKEN_MISSING,
  /// error while parsing oauth response, the token secret field is unavailable
  DT_OAUTH_PARSE_ERROR_SECRET_MISSING
} dt_oauth_errors_t;

/**
 * context option
 * available options are:
 *   * DT_OAUTH_USE_AUTHORIZE_HEADER (boolean) use the Authorize header to sign
 *     request when set to 1
 *
 *   * DT_OAUTH_USE_ALTERNATIVE_ENDPOINT (char*) send request to an other
 *     endpoint than the one specified on init. requests will still be signed with
 *     the original endpoint
 */
typedef enum dt_oauth_opt_t{
  DT_OAUTH_USE_AUTHORIZE_HEADER,
  DT_OAUTH_USE_ALTERNATIVE_ENDPOINT
} dt_oauth_opt_t;


/**
 * type of callback function called by dt_oauth_get/post/post_file. @a reply is the response body from the server
 *
 * @param ctx oauth context
 * @param reply null terminated buffer containing the reply from the server
 * @param httpcode response code from the server
 * @param data user data passed to callback
 * @return *MUST* returns 0 on success, error code on failure. user error codes SHOULD be above 1000
 */
typedef int (*dt_oauth_reply_callback_t) (dt_oauth_ctx_t *ctx, int httpcode, const char *reply, gpointer data);

/**
 * type of callback function called by dt_oauth_request_token and dt_oauth_access_token
 *
 * @param ctx oauth context
 * @param param dictionary containing parsed parametters
 * @param data user data passed to callback
 * @return *MUST* returns 0 on success, error code on failure. user error codes SHOULD be above 1000
 */
typedef int (*dt_oauth_param_callback) (dt_oauth_ctx_t *ctx, GHashTable *params, gpointer data);


/**
 * initialize a new oauth context
 *
 * @param endpoint main url of the service every following calls will use this as prefix for their url
 * @param consumer_key the application consumer key
 * @param consumer_key the application consumer secret
 * @return a newly allocated context
 */
dt_oauth_ctx_t *dt_oauth_ctx_init(const char *endpoint, const char *consumer_key, const char *consumer_secret);

/**
 * release given oauth context
 */
void dt_oauth_ctx_destroy(dt_oauth_ctx_t *ctx);

/**
 * specify an option @see dt_oauth_opt_t for available options
 * @param ctx
 * @param opt
 * @param value
 * @return 0 on success
 */
int dt_oauth_set_opt(dt_oauth_ctx_t *ctx, dt_oauth_opt_t opt, const void *value);

/**
 *
 *
 * @param method "GET" or "POST", define the type of request
 * @param service the webservices url to call
 * @param extraparam parametters to be added to the request, it *MUST* be
 *                   a null terminated array of string, strings comes by
 *                   pair the first one is the parameter name and the second
 *                   is the parameter value. These values must NOT be urlencoded
 *
 * @param callback this callback is call if the request succeed and is correctly
 *                 parsed, use it to extract extra parameters. If this value might
 *                 be NULL if no callback is required
 *
 * @param callbackdata data to be passed to the callback function
 * @return 0 on success, error code otherwise
 */
int dt_oauth_request_token(dt_oauth_ctx_t *ctx, const char *method, const char *service, const char **extraparam, dt_oauth_param_callback callback, gpointer callbackdata);


/**
 * exchange request tokens for access tokens
 *
 * @param method "GET" or "POST", define the type of request
 * @param service the webservices url to call
 * @param extraparam parametters to be added to the request, it *MUST* be
 *                   a null terminated array of string, strings comes by
 *                   pair the first one is the parameter name and the second
 *                   is the parameter value. These values must NOT be urlencoded
 *
 * @param callback this callback is call if the request succeed and is correctly
 *                 parsed, use it to extract extra parameters. If this value might
 *                 be NULL if no callback is required
 *
 * @param callbackdata data to be
 * @return 0 on success, error code else
 */
int dt_oauth_access_token(dt_oauth_ctx_t *ctx, const char *method, const char *service, const char **extraparam, dt_oauth_param_callback callback, gpointer callbackdata);

/**
 * set or overwrite current user token
 *
 * @param token_key user token key
 * @param token_key user token secret
 * @return 0 on success
 */
int dt_oauth_set_token(dt_oauth_ctx_t *ctx, const char *token_key, const char *token_secret);

/**
 * perfom a GET request signed with oauth credentials
 *
 * @param service the webservices url to call
 * @param extraparam parametters to be added to the request, it *MUST* be
 *                   a null terminated array of string, strings comes by
 *                   pair the first one is the parameter name and the second
 *                   is the parameter value. These values must NOT be urlencoded
 *
 * @param callback this callback is call if the request succeed and is correctly
 *                 parsed, use it to extract extra parameters. If this value might
 *                 be NULL if no callback is required
 *
 * @param callbackdata data to be passed to the callback function
 * @return 0 on success, error code otherwise
 */
int dt_oauth_get(dt_oauth_ctx_t *ctx, const char *service, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata);

/**
 * perfom a POST request signed with oauth credentials
 *
 * @param service the webservices url to call
 * @param extraparam parametters to be added to the request, it *MUST* be
 *                   a null terminated array of string, strings comes by
 *                   pair the first one is the parameter name and the second
 *                   is the parameter value. These values must NOT be urlencoded
 *
 * @param callback this callback is call if the request succeed and is correctly
 *                 parsed, use it to extract extra parameters. If this value might
 *                 be NULL if no callback is required
 *
 * @param callbackdata data to be passed to the callback function
 * @return 0 on success, error code otherwise
 */
int dt_oauth_post(dt_oauth_ctx_t *ctx, const char *service, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata);


/**
 *
 * upload files to a webservice. the request is signed with oauth credentials
 *
 * @warning this is a NON STANDARD oauth method, file uploading and multipart-form
 * aren't part of oauth1.0. It use a multipart-form encoding instead a form-urlencoded
 * to transmit *ALL* post fields, oauth_ included. files are *NOT* part of the oauth
 * signature (they aren't hashed). please refer to the documentation of the web service
 * to know if you can use this method.
 *
 * @param service the webservices url to call
 * @param extraparam parametters to be added to the request, it *MUST* be
 *                   a null terminated array of string, strings comes by
 *                   pair the first one is the parameter name and the second
 *                   is the parameter value. These values must NOT be urlencoded
 *
 * @param files the list of files to be posted, it *MUST* be a null terminated
 *              array of strings, strings comes by pairs: the first one is the
 *              field name of the multipart form, the second is the file path.
 *
 * @param callback this callback is call if the request succeed and is correctly
 *                 parsed, use it to extract extra parameters. If this value might
 *                 be NULL if no callback is required
 *
 * @param callbackdata data to be passed to the callback function
 * @return 0 on success, error code otherwise
 */
int dt_oauth_post_files(dt_oauth_ctx_t *ctx, const char *service, const char **files, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
