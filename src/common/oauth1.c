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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <oauth.h>
#include <curl/curl.h>
#include "oauth1.h"


typedef struct SubCallbackData
{
  DtOAUthParamCallback callback;
  gpointer data;
} SubCallbackData;


DtOAUthCtx *dt_oauth_ctx_init(const char *endpoint, const char *consumer_key, const char *consumer_secret)
{
  DtOAUthCtx *ctx = (DtOAUthCtx*)g_malloc0(sizeof(DtOAUthCtx));
  ctx->endpoint = g_strdup(endpoint);
  ctx->consumer_key = g_strdup(consumer_key);
  ctx->consumer_secret = g_strdup(consumer_secret);
  ctx->use_authorize_header = FALSE;
  return ctx;
}

void dt_oauth_ctx_destroy(DtOAUthCtx *ctx)
{
  g_free(ctx->endpoint);
  g_free(ctx->consumer_key);
  g_free(ctx->consumer_secret);
  if (ctx->token_key != NULL)
    g_free(ctx->token_key);
  if (ctx->token_secret != NULL)
    g_free(ctx->token_secret);
  if (ctx->alternative_endpoint != NULL)
    g_free(ctx->alternative_endpoint);
  g_free(ctx);
}

int dt_oauth_set_opt(DtOAUthCtx *ctx, DtOauthOpt opt, const void *value)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  switch (opt)
  {
    case DT_OAUTH_USE_AUTHORIZE_HEADER:
    {
      ctx->use_authorize_header = (int)value;
      break;
    }
    case DT_OAUTH_USE_ALTERNATIVE_ENDPOINT:
    {
      ctx->alternative_endpoint = g_strdup((const char*)value);
      break;
    }
    default:
      return DT_OAUTH_INVALID_OPTION;
      break;
  }
  return DT_OAUTH_OK;
}

int dt_oauth_set_token(DtOAUthCtx *ctx, const char *token_key, const char *token_secret)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  if (ctx->token_key)
    g_free(ctx->token_key);
  ctx->token_key = g_strdup(token_key);

  if (ctx->token_secret)
    g_free(ctx->token_secret);
  ctx->token_secret= g_strdup(token_secret);
  return DT_OAUTH_OK;
}

static GHashTable *dt_oauth_urlencoded_to_table(const char *url)
{
  GHashTable *paramtable = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)free);

  char **urlchunks = g_strsplit_set(url, "&=", -1);
  int i = 0;
  for (i = 0; urlchunks[i] != NULL && urlchunks[i + 1] != NULL; i += 2)
  {
    g_hash_table_insert(paramtable, g_strdup(urlchunks[i]), oauth_url_unescape(urlchunks[i+1], NULL));
  }
  g_strfreev(urlchunks);

  return paramtable;
}

static char *dt_oauth_gen_autorize_header(const char *url, const char *params, char **remainings)
{
  GString *header = g_string_new("Authorization: OAuth realm=\"");
  g_string_append(header, url);
  g_string_append(header, "\",");
  char **urlchunks = g_strsplit_set(params, "&=", -1);
  gboolean saveremainings = (remainings != NULL);
  GString *remainingsparams = g_string_new("");

  int i = 0;
  for (i = 0; urlchunks[i] != NULL && urlchunks[i + 1] != NULL; i += 2)
  {
    if (strncmp(urlchunks[i], "oauth_", 6) == 0)
    {
      g_string_append_c(header, ',');
      g_string_append(header, urlchunks[i]);
      g_string_append(header, "=\"");
      g_string_append(header, urlchunks[i+1]);
      g_string_append_c(header, '"');
    }
    else if (saveremainings)
    {
      if (i != 0)
        g_string_append_c(remainingsparams, '&');
      g_string_append(remainingsparams, urlchunks[i]);
      g_string_append(remainingsparams, "=");
      g_string_append(remainingsparams, urlchunks[i+1]);
    }
  }
  g_strfreev(urlchunks);
  char *ret = header->str;
  if (saveremainings)
  {
    *remainings = remainingsparams->str;
    g_string_free(remainingsparams, FALSE);
  }
  g_string_free(header, FALSE);
  return ret;
}


static size_t curl_write_data_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  GString *string = (GString*) data;
  g_string_append_len(string, ptr, size * nmemb);
  return size * nmemb;
}

static int curl_query_get(DtOAUthCtx *ctx, const char *url, char *params, DtOAUthReplyCallback callback, gpointer callbackdata)
{
  GString *response = g_string_new("");
  struct curl_slist *slist=NULL;

  const char *actualparam = params;
  gboolean freeparam = FALSE;
  if (ctx->use_authorize_header)
  {
    char *getparams = NULL;
    char *authorizeheader = dt_oauth_gen_autorize_header(url, params, &getparams);
    if ((getparams != NULL) && (getparams[0] != '\0'))
    {
      actualparam = getparams;
      freeparam = TRUE;
    }
    else
      actualparam = NULL;
    slist = curl_slist_append(slist, authorizeheader);
  }

  const char *actualurl = url;
  gboolean freeurl = FALSE;
  if (actualparam)
  {
    freeurl = TRUE;
    actualurl = g_strconcat(url, "?", actualparam, NULL);
  }
  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, actualurl);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);

  if (slist != NULL)
  {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
  }
  int res = curl_easy_perform(curl);

  if (res != CURLE_OK)
    goto cleanup;

  char *response_data =  response->str;
  //parse the response
  res = callback(ctx, response_data, callbackdata);

  cleanup:
  if (freeparam)
    g_free((char*)actualparam);
  if (freeurl)
    g_free((char*)actualurl);
  g_string_free(response, TRUE);
  curl_easy_cleanup(curl);
  curl_slist_free_all(slist);
  return res;
}


static gboolean curl_query_post(DtOAUthCtx *ctx, const gchar *url, const char *params, const char **files, DtOAUthReplyCallback callback, gpointer callbackdata)
{
  GString *response = g_string_new("");
  struct curl_slist *slist=NULL;

  const char *actualparam = params;
  gboolean freeparam = FALSE;
  if (ctx->use_authorize_header)
  {
    char *getparams = NULL;
    char *authorizeheader = dt_oauth_gen_autorize_header(url, params, &getparams);
    if (getparams[0] != '\0')
    {
      freeparam = TRUE;
      actualparam = getparams;
    }
    else
      actualparam = NULL;
    slist = curl_slist_append(slist, authorizeheader);
  }

  CURL* curl = curl_easy_init();
  struct curl_httppost *formpost = NULL;
  struct curl_httppost *formpost_last  = NULL;

  if (files) //use multipart/form-data
  {
    int i = 0;
    for (; files[i] != NULL && files[i+1]; i += 2)
    {
      curl_formadd(&(formpost),
        &(formpost_last),
        CURLFORM_COPYNAME, files[i],
        CURLFORM_FILE, files[i+1],
        CURLFORM_END);
    }

    if (actualparam)
    {
      char **urlchunks = g_strsplit_set(actualparam, "&=", -1);
      for (i = 0; urlchunks[i] != NULL && urlchunks[i + 1] != NULL; i += 2)
      {
        char *escapedparam = oauth_url_unescape(urlchunks[i+1], NULL);
        curl_formadd(&(formpost),
          &(formpost_last),
          CURLFORM_COPYNAME, urlchunks[i],
          CURLFORM_COPYCONTENTS, escapedparam,
          CURLFORM_END);
        free(escapedparam);
      }
      g_strfreev(urlchunks);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
  }
  else if (actualparam) //use application/x-www-form-urlencoded as specified by oauth
  {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, actualparam);
  }

  if (!ctx->alternative_endpoint)
    curl_easy_setopt(curl, CURLOPT_URL, url);
  else
  {
    GString *alternativeurl = g_string_new(ctx->alternative_endpoint);
    g_string_append(alternativeurl, url + strlen(ctx->endpoint));
    curl_easy_setopt(curl, CURLOPT_URL, alternativeurl->str);
    g_string_free(alternativeurl, TRUE);
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);

  if (slist != NULL)
  {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
  }

  int res = curl_easy_perform(curl);

  if (res != CURLE_OK)
    goto cleanup;

  char *response_data =  response->str;
  res = callback(ctx, response_data, callbackdata);

  cleanup:
  g_string_free(response, TRUE);
  curl_formfree(formpost);
  curl_slist_free_all(slist);
  curl_easy_cleanup(curl);
  if (freeparam)
    g_free((char*)actualparam);
  return res;
}

static int dt_oauth_parse_urlencoded_reply(DtOAUthCtx *ctx, const gchar *reply, SubCallbackData *subcallback)
{
  g_return_val_if_fail(subcallback != NULL, -1);
  int ret = DT_OAUTH_OK;
  GHashTable *paramtable = dt_oauth_urlencoded_to_table(reply);
  if (subcallback && subcallback->callback) //should always be true
    ret = subcallback->callback(ctx, paramtable, subcallback->data);
  g_hash_table_destroy(paramtable);
  return ret;
}

static int dt_oauth_update_ctx_token_cb(DtOAUthCtx *ctx, GHashTable *params, SubCallbackData *subcallback)
{
  char *oauth_token = g_hash_table_lookup(params, "oauth_token");
  if (oauth_token == NULL)
    return DT_OAUTH_PARSE_ERROR_TOKEN_MISSING;
  char *oauth_token_secret = (char*)g_hash_table_lookup(params, "oauth_token_secret");
  if (oauth_token_secret == NULL)
    return DT_OAUTH_PARSE_ERROR_SECRET_MISSING;
  if (ctx->token_key)
    g_free(ctx->token_key);
  if (ctx->token_secret)
    g_free(ctx->token_secret);
  ctx->token_key = g_strdup(oauth_token);
  ctx->token_secret = g_strdup(oauth_token_secret);
  if (subcallback && subcallback->callback)
  {
    return subcallback->callback(ctx, params, subcallback->data);
  }
  else
  {
    return DT_OAUTH_OK;
  }
}

static char *dt_oauth_build_url(const char *baseurl, const char *service, const char **params)
{
  GString *url = g_string_new(baseurl);
  g_string_append(url, service);
  if (params != NULL)
  {
    int i = 0;
    for (; params[i] != 0; i++)
    {
      if ((i == 0) && (strstr(url->str, "?") == NULL))
      {
        g_string_append_c(url, '?');
      }
      else if (i % 2 == 0) //also valid when i == 0 and '?' in url
        g_string_append_c(url, '&');
      else
        g_string_append_c(url, '=');
      g_string_append(url, oauth_url_escape(params[i]));
    }
  }
  char *url_str = url->str;
  g_string_free(url, FALSE);
  return url_str;
}


int dt_oauth_get(DtOAUthCtx *ctx, const char *service, const char **extraparam, DtOAUthReplyCallback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  char *url = dt_oauth_build_url(ctx->endpoint, service, extraparam);
  char *params = NULL;
  char *req_url = oauth_sign_url2(url, &params, OA_HMAC, "GET", ctx->consumer_key, ctx->consumer_secret, ctx->token_key, ctx->token_secret);
  int ret = curl_query_get(ctx, req_url, params, callback, callbackdata);
  g_free(url);
  free(req_url);
  return ret;
}

int dt_oauth_post(DtOAUthCtx *ctx, const char *service, const char **extraparam, DtOAUthReplyCallback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  char *url = dt_oauth_build_url(ctx->endpoint, service, extraparam);
  char *params;
  char *req_url = oauth_sign_url2(url, &params, OA_HMAC, "POST", ctx->consumer_key, ctx->consumer_secret, ctx->token_key, ctx->token_secret);
  int ret = curl_query_post(ctx, req_url, params, NULL, callback, callbackdata);
  g_free(url);
  free(req_url);
  return ret;
}

int dt_oauth_post_files(DtOAUthCtx *ctx, const char *service, const char **files, const char **extraparam, DtOAUthReplyCallback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  char *url = dt_oauth_build_url(ctx->endpoint, service, extraparam);
  char *params;
  char *req_url = oauth_sign_url2(url, &params, OA_HMAC, "POST", ctx->consumer_key, ctx->consumer_secret, ctx->token_key, ctx->token_secret);
  int ret = curl_query_post(ctx, req_url, params, files, callback, callbackdata);
  g_free(url);
  free(req_url);
  return ret;
}

int dt_oauth_request_token(DtOAUthCtx *ctx, const char *method, const char *service, const char **extraparam, DtOAUthParamCallback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  SubCallbackData subcallback = { callback, callbackdata };
  SubCallbackData update_oauth_info_cb = { (DtOAUthParamCallback)dt_oauth_update_ctx_token_cb, &subcallback};
  if (method == NULL || g_strcmp0(method, "GET") == 0)
    return dt_oauth_get(ctx, service, extraparam, (DtOAUthReplyCallback)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
  else
    return dt_oauth_post(ctx, service, extraparam, (DtOAUthReplyCallback)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
}

int dt_oauth_access_token(DtOAUthCtx *ctx, const char *method, const char *service, const char **extraparam, DtOAUthParamCallback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  SubCallbackData subcallback = { callback, callbackdata };
  SubCallbackData update_oauth_info_cb = { (DtOAUthParamCallback)dt_oauth_update_ctx_token_cb, &subcallback};
  if (method == NULL || g_strcmp0(method, "GET") == 0)
    return dt_oauth_get(ctx, service, extraparam, (DtOAUthReplyCallback)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
  else
    return dt_oauth_post(ctx, service, extraparam, (DtOAUthReplyCallback)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
}

