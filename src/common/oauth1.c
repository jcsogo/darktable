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
#include <curl/curl.h>
#include "oauth1.h"

typedef struct dt_oauth_ctx_priv_t
{
  GRand *rand;
  CURL *curl;
} dt_oauth_ctx_priv_t;

#define RESERVED_URI_CHARS "_.-~"

typedef struct subcallbackdata_t
{
  dt_oauth_param_callback callback;
  gpointer data;
} subcallbackdata_t;


dt_oauth_ctx_t *dt_oauth_ctx_init(const char *endpoint, const char *consumer_key, const char *consumer_secret)
{
  dt_oauth_ctx_t *ctx = (dt_oauth_ctx_t*)g_malloc0(sizeof(dt_oauth_ctx_t));
  ctx->endpoint = g_strdup(endpoint);
  ctx->consumer_key = g_strdup(consumer_key);
  ctx->consumer_secret = g_strdup(consumer_secret);
  ctx->use_authorize_header = FALSE;
  ctx->priv = (dt_oauth_ctx_priv_t*)g_malloc0(sizeof(dt_oauth_ctx_priv_t));
  ctx->priv->rand = g_rand_new();
  ctx->priv->curl = curl_easy_init();
  return ctx;
}

void dt_oauth_ctx_destroy(dt_oauth_ctx_t *ctx)
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
  g_rand_free(ctx->priv->rand);
  curl_easy_cleanup(ctx->priv->curl);
  g_free(ctx->priv);
  g_free(ctx);
}

int dt_oauth_set_opt(dt_oauth_ctx_t *ctx, dt_oauth_opt_t opt, gpointer value)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  switch (opt)
  {
    case DT_OAUTH_USE_AUTHORIZE_HEADER:
    {
      ctx->use_authorize_header = GPOINTER_TO_INT(value);
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

int dt_oauth_set_token(dt_oauth_ctx_t *ctx, const char *token_key, const char *token_secret)
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
    g_hash_table_insert(paramtable, g_strdup(urlchunks[i]), g_uri_unescape_string(urlchunks[i+1], RESERVED_URI_CHARS));
  }
  g_strfreev(urlchunks);

  return paramtable;
}

static size_t dt_curl_write_data_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  GString *string = (GString*) data;
  g_string_append_len(string, ptr, size * nmemb);
  return size * nmemb;
}

static gboolean dt_dt_curl_query_get_build_get_url(const char *key, const char *value, GString *url)
{
  g_string_append_uri_escaped(url, key, RESERVED_URI_CHARS, FALSE);
  g_string_append_c(url, '=');
  g_string_append_uri_escaped(url, value, RESERVED_URI_CHARS, FALSE);
  g_string_append_c(url, '&');
  return FALSE;
}

static gboolean dt_gen_authorize_header_find_oauth(char *key, const char *value, GSList **list)
{
  if (strncmp(key, "oauth_", 6) == 0)
    *list = g_slist_append(*list, key);
  return FALSE;
}

static char *dt_oauth_gen_autorize_header(const char *url, GTree *params)
{
  GSList *oauthheaderlist = NULL;
  GString *header = g_string_new("Authorization: OAuth realm=\"");
  g_string_append(header, url);
  g_string_append(header, "\",");

  g_tree_foreach(params, (GTraverseFunc)dt_gen_authorize_header_find_oauth, &oauthheaderlist);
  GSList *it;
  for (it = oauthheaderlist; it != NULL; it = it->next)
  {
    const char *key = it->data;
    g_string_append(header, key);
    g_string_append(header, "=\"");
    g_string_append_uri_escaped(header, g_tree_lookup(params, key), RESERVED_URI_CHARS, FALSE);
    g_string_append_c(header, '"');
    if (it->next != NULL)
      g_string_append_c(header, ',');
    g_tree_remove(params, key);
  }
  g_slist_free(oauthheaderlist);
  char *headstr = header->str;
  g_string_free(header, FALSE);
  return headstr;
}

static int dt_curl_query_get(dt_oauth_ctx_t *ctx, const char *url, GTree *params, dt_oauth_reply_callback_t callback, gpointer callbackdata)
{
  GString *response = g_string_new("");
  struct curl_slist *slist = NULL;

  if (ctx->use_authorize_header)
  {
    char *authorizeheader = dt_oauth_gen_autorize_header(url, params);
    slist = curl_slist_append(slist, authorizeheader);
  }

  GString *actualurl = g_string_new(url);
  if (g_tree_nnodes(params) > 0)
  {
    g_string_append_c(actualurl, '?');
    g_tree_foreach(params, (GTraverseFunc)dt_dt_curl_query_get_build_get_url, actualurl);
    g_string_truncate(actualurl, actualurl->len -1);//remove extra '?'
  }
  curl_easy_reset(ctx->priv->curl);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_URL, actualurl->str);
#ifdef VERBOSE
  curl_easy_setopt(ctx->priv->curl, CURLOPT_VERBOSE, 2);
#endif
  curl_easy_setopt(ctx->priv->curl, CURLOPT_WRITEFUNCTION, dt_curl_write_data_cb);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_FOLLOWLOCATION, TRUE);

  if (slist != NULL)
  {
    curl_easy_setopt(ctx->priv->curl, CURLOPT_HTTPHEADER, slist);
  }
  int res = curl_easy_perform(ctx->priv->curl);

  if (res != CURLE_OK)
    goto cleanup;

  char *response_data =  response->str;
  long responsecode = 0;
  curl_easy_getinfo(ctx->priv->curl, CURLINFO_RESPONSE_CODE, &responsecode);

  //parse the response
  res = callback(ctx, responsecode, response_data, callbackdata);

  cleanup:
  g_string_free(actualurl, TRUE);
  g_string_free(response, TRUE);
  curl_easy_reset(ctx->priv->curl);
  curl_slist_free_all(slist);
  return res;
}

typedef struct curlhttppostformcbdata_t
{
  struct curl_httppost **formpost;
  struct curl_httppost **formpost_last;
} curlhttppostformcbdata_t;

static gboolean dt_dt_curl_query_post_add_multipart_param(const char *key, const char *value, curlhttppostformcbdata_t *data)
{
  curl_formadd(data->formpost,
    data->formpost_last,
    CURLFORM_COPYNAME, key,
    CURLFORM_COPYCONTENTS, value,
    CURLFORM_END);
  return FALSE;
}

static gboolean dt_curl_query_post(dt_oauth_ctx_t *ctx, const gchar *url, GTree *params, const char **files, dt_oauth_reply_callback_t callback, gpointer callbackdata)
{
  GString *response = g_string_new("");
  struct curl_slist *slist = NULL;
  char *formencodedparams = NULL;

  curl_easy_reset(ctx->priv->curl);
  struct curl_httppost *formpost = NULL;
  struct curl_httppost *formpost_last  = NULL;

  if (!ctx->alternative_endpoint)
    curl_easy_setopt(ctx->priv->curl, CURLOPT_URL, url);
  else
  {
    GString *alternativeurl = g_string_new(ctx->alternative_endpoint);
    g_string_append(alternativeurl, url + strlen(ctx->endpoint));
    curl_easy_setopt(ctx->priv->curl, CURLOPT_URL, alternativeurl->str);
    g_string_free(alternativeurl, TRUE);
  }

  if (ctx->use_authorize_header)
  {
    char *authorizeheader = dt_oauth_gen_autorize_header(url, params);
    slist = curl_slist_append(slist, authorizeheader);
  }

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

    if (g_tree_nnodes(params) > 0)
    {
      curlhttppostformcbdata_t cbdata = { &formpost, &formpost_last };
      g_tree_foreach(params, (GTraverseFunc)dt_dt_curl_query_post_add_multipart_param, &cbdata);
    }
    curl_easy_setopt(ctx->priv->curl, CURLOPT_HTTPPOST, formpost);
  }
  else if (g_tree_nnodes(params) > 0) //use application/x-www-form-urlencoded as specified by oauth
  {
    GString *formencoded_params = g_string_new("");
    g_tree_foreach(params, (GTraverseFunc)dt_dt_curl_query_get_build_get_url, formencoded_params);
    g_string_truncate(formencoded_params, formencoded_params->len -1);//remove extra '?'
    formencodedparams = formencoded_params->str;
    curl_easy_setopt(ctx->priv->curl, CURLOPT_POSTFIELDS, formencoded_params->str);
    g_string_free(formencoded_params, FALSE);
  }

#ifdef VERBOSE
  curl_easy_setopt(ctx->priv->curl, CURLOPT_VERBOSE, 2);
#endif
  curl_easy_setopt(ctx->priv->curl, CURLOPT_WRITEFUNCTION, dt_curl_write_data_cb);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_easy_setopt(ctx->priv->curl, CURLOPT_FOLLOWLOCATION, TRUE);

  if (slist != NULL)
  {
    curl_easy_setopt(ctx->priv->curl, CURLOPT_HTTPHEADER, slist);
  }

  int res = curl_easy_perform(ctx->priv->curl);

  if (res != CURLE_OK)
    goto cleanup;

  char *response_data =  response->str;
  long responsecode = 0;
  curl_easy_getinfo(ctx->priv->curl, CURLINFO_RESPONSE_CODE, &responsecode);

  res = callback(ctx, responsecode, response_data, callbackdata);

  cleanup:
  if (formencodedparams)
    g_free(formencodedparams);
  g_string_free(response, TRUE);
  curl_formfree(formpost);
  curl_slist_free_all(slist);
  curl_easy_reset(ctx->priv->curl);
  return res;
}

static int dt_oauth_parse_urlencoded_reply(dt_oauth_ctx_t *ctx, int code, const gchar *reply, subcallbackdata_t *subcallback)
{
  g_return_val_if_fail(subcallback != NULL, -1);
  int ret = DT_OAUTH_OK;
  if (code != 200)
    return -1;//FIXME better return code
  GHashTable *paramtable = dt_oauth_urlencoded_to_table(reply);
  if (subcallback && subcallback->callback) //should always be true
    ret = subcallback->callback(ctx, paramtable, subcallback->data);
  g_hash_table_destroy(paramtable);
  return ret;
}

static int dt_oauth_update_ctx_token_cb(dt_oauth_ctx_t *ctx, GHashTable *params, subcallbackdata_t *subcallback)
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

/**
 * refactored from liboauth:
 *    - use glib functions (urlencode + alloc)
 *    - handle some more case from OAUth sections 9.1.2 (uri case + htpps port)
 *    - returns a the base url + an ordered tree
 *
 * splits the given url into a parameter array.
 *
 * NOTE: Request-parameters-values may include an ampersand character.
 * However if unescaped this function will use them as parameter delimiter.
 * If you need to make such a request, this function since version 0.3.5 allows
 * to use the ASCII SOH (0x01) character as alias for '&' (0x26).
 * (the motivation is convenience: SOH is /untypeable/ and much more
 * unlikely to appear than '&' - If you plan to sign fancy URLs you
 * should not split a query-string, but rather provide the parameter array
 * directly to \ref oauth_serialize_url)
 *
 * @param url the url or query-string to parse.
 * @param argv pointer to a (char *) array where the results are stored.
 *  The array is re-allocated to match the number of parameters and each
 *  parameter-string is allocated with strdup. - The memory needs to be freed
 *  by the caller.
 * @param qesc use query parameter escape (vs post-param-escape) - if set
 *        to 1 all '+' are treated as spaces ' '
 *
 * @return number of parameter(s) in array.
 */
static char *dt_oauth_split_url(const char *url, GTree *urlparams, short qesc) {
  int argc=0;
  char *token;
  char *tmp;
  char *t1;
  char *baseurl = NULL;

  if (!url)
    return 0;

  t1=g_strdup(url);

  // '+' represents a space, in a URL query string
  while ((qesc&1) && (tmp=strchr(t1,'+')))
    *tmp=' ';

  tmp=t1;
  while((token=strtok(tmp,"&?")))
  {
    if(!strncmp("oauth_signature=",token,16))
      continue;

    while (!(qesc&2) && (tmp=strchr(token,'\001')))
      *tmp='&';

    if (argc>0 || !strstr(token, ":/"))
    {
      int i = 0;
      for (; token[i] != '\0'; i++)
        if (token[i] == '=')
          break;
      if (token[i] == '\0') //oops &param& malformed url
        return NULL; //FIXME

      gchar *key = g_strndup(token, i);
      gchar *value = NULL;

      if (qesc&4)
        value = g_uri_unescape_string(token + i + 1, RESERVED_URI_CHARS);
      else
        value = g_strdup(token + i + 1);
      g_tree_insert(urlparams, key, value);
    }
    else if (argc==0)
    {

      // HTTP does not allow empty absolute paths, so the URL
      // 'http://example.com' is equivalent to 'http://example.com/' and should
      // be treated as such for the purposes of OAuth signing (rfc2616, section 3.2.1)
      // see http://groups.google.com/group/oauth/browse_thread/thread/c44b6f061bfd98c?hl=en
      char *slash=strstr(token, ":/");
      while (slash && *(++slash) == '/')
        ; // skip slashes eg /xxx:[\/]*/
#if 0
      // skip possibly unescaped slashes in the userinfo - they're not allowed by RFC2396 but have been seen.
      // the hostname/IP may only contain alphanumeric characters - so we're safe there.
      if (slash && strchr(slash,'@'))
        slash=strchr(slash,'@');
#endif
      if (slash && !strchr(slash,'/'))
      {
        baseurl = g_strconcat(token, "/", NULL);
      }
      else
        baseurl = g_strdup(token);

      if ((tmp=strstr(baseurl, ":80/")))  //HTTP port must be removed
        g_memmove(tmp, tmp+3, strlen(tmp+2));
      if ((tmp=strstr(baseurl, ":443/")))  //HTTPS port must be removed
        g_memmove(tmp, tmp+4, strlen(tmp+3));

      //scheme must be lower case
      char *begin = baseurl;
      char *end = strstr(begin, "://");
      char *it = begin;
      for (; it != end; it++)
        *it = g_ascii_tolower(*it);

      //authority must be lower case
      begin = strchr(begin, '@');
      if (begin == NULL)
        begin = end + 3;
      end = strchr(begin, '/');
      for (it = begin; it != end; it++)
        *it = g_ascii_tolower(*it);
    }

    tmp=NULL;
    argc++;
  }

  g_free(t1);
  return baseurl;
}

static gchar *dt_oauth_gen_nonce(dt_oauth_ctx_t *ctx, size_t size)
{
  gchar *nonce = (gchar*)g_malloc((size + 1) * sizeof(gchar));
  nonce[size] = '\0';
  int i = 0;
  for (; i < size; i++)
  {
    nonce[i] = (gchar) g_rand_int_range(ctx->priv->rand, 'a', 'z');
  }
  return nonce;
}

static gchar *dt_oauth_gen_timestamp()
{
  gint64 timestamp = g_get_real_time() / 1000000;
  return g_strdup_printf("%ld", timestamp);
}

static void dt_oauth_add_oauth_params(dt_oauth_ctx_t *ctx, GTree *urlparams)
{
  if ((ctx->consumer_key != NULL) && (g_tree_lookup(urlparams, "oauth_token") == NULL))
    g_tree_insert(urlparams, g_strdup("oauth_consumer_key"), g_strdup(ctx->consumer_key));

  if ((ctx->token_key != NULL) && (g_tree_lookup(urlparams, "oauth_token") == NULL))
    g_tree_insert(urlparams, g_strdup("oauth_token"), g_strdup(ctx->token_key));

  if (g_tree_lookup(urlparams, "oauth_nonce") == NULL)
    g_tree_insert(urlparams, g_strdup("oauth_nonce"), dt_oauth_gen_nonce(ctx, 20));

  if (g_tree_lookup(urlparams, "oauth_timestamp") == NULL)
    g_tree_insert(urlparams, g_strdup("oauth_timestamp"), dt_oauth_gen_timestamp());

  if (g_tree_lookup(urlparams, "oauth_version") == NULL)
    g_tree_insert(urlparams, g_strdup("oauth_version"), g_strdup("1.0"));

  if (g_tree_lookup(urlparams, "oauth_signature_method") == NULL)
    g_tree_insert(urlparams, g_strdup("oauth_signature_method"), g_strdup("HMAC-SHA1"));
}


static gboolean dt_oauth_sign_url_signdata_cb(char *key, char *value, GString *signdata)
{
  g_string_append_uri_escaped(signdata, key, RESERVED_URI_CHARS, FALSE);
  g_string_append(signdata, "=");
  g_string_append_uri_escaped(signdata, value, RESERVED_URI_CHARS, FALSE);
  g_string_append(signdata, "&");
  return FALSE;
}

static char *dt_oauth_sign_url(dt_oauth_ctx_t *ctx, const char *url, const char **postargs, const char *http_method, GTree *params)
{
  int i;
  if (postargs != NULL)
    for (i = 0; postargs[i] != NULL && postargs[i + 1] != NULL; i += 2)
    {
      g_tree_insert(params, g_strdup(postargs[i]), g_uri_escape_string(postargs[i+1], RESERVED_URI_CHARS, FALSE));
    }
  dt_oauth_add_oauth_params(ctx,params);
  char *method = dt_oauth_split_url(url, params, 5);


  GString *signdata = g_string_new(http_method);
  g_string_append_c(signdata, '&');
  g_string_append_uri_escaped(signdata, method, RESERVED_URI_CHARS, FALSE);
  g_string_append_c(signdata, '&');

  GString *signparam = g_string_new("");
  g_tree_foreach(params, (GTraverseFunc)dt_oauth_sign_url_signdata_cb, signparam);  
  if (signparam->str[signparam->len -1] == '&')
    g_string_truncate(signparam, signparam->len - 1); //remove extra &
  g_string_append_uri_escaped(signdata, signparam->str, RESERVED_URI_CHARS, FALSE);//we have to re-escape the parametters
#ifdef VERBOSE
  printf("normalized params: %s\n", signparam->str);
  printf("signature base string: %s\n", signdata->str);
#endif
  g_string_free(signparam, TRUE);

  GString *key = g_string_new("");
  g_string_append_uri_escaped(key, ctx->consumer_secret, RESERVED_URI_CHARS, FALSE);
  g_string_append_c(key, '&');
  if (ctx->token_secret)
  g_string_append_uri_escaped(key, ctx->token_secret, RESERVED_URI_CHARS, FALSE);


  char *sig_hex= g_compute_hmac_for_string(G_CHECKSUM_SHA1, (const guchar*)key->str, key->len, (const gchar*)signdata->str, signdata->len);
  char sig_bin[20];
  for (i = 0; i < 20; i++)
    sscanf(sig_hex + (2*i), "%2hhx", &sig_bin[i]);
  char *sig64 = g_base64_encode((const guchar*)sig_bin, 20);
  g_free(sig_hex);

  g_tree_insert(params, g_strdup("oauth_signature"), sig64);
  g_string_free(signdata, TRUE);
  g_string_free(key, TRUE);
  return method;
}


int dt_oauth_get(dt_oauth_ctx_t *ctx, const char *service, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);

  GTree *params = g_tree_new_full((GCompareDataFunc)g_strcmp0, NULL, g_free, g_free);
  char *url = g_strconcat(ctx->endpoint, service, NULL);

  char *baseurl = dt_oauth_sign_url(ctx, url, extraparam, "GET", params);
  int ret = dt_curl_query_get(ctx, baseurl, params, callback, callbackdata);
  g_free(url);
  g_free(baseurl);
  g_tree_destroy(params);
  return ret;
}

int dt_oauth_post(dt_oauth_ctx_t *ctx, const char *service, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);

  GTree *params = g_tree_new_full((GCompareDataFunc)g_strcmp0, NULL, g_free, g_free);
  char *url = g_strconcat(ctx->endpoint, service, NULL);

  char *baseurl = dt_oauth_sign_url(ctx, url, extraparam, "POST", params);
  int ret = dt_curl_query_post(ctx, baseurl, params, NULL, callback, callbackdata);
  g_free(url);
  g_free(baseurl);
  g_tree_destroy(params);
  return ret;
}

int dt_oauth_post_files(dt_oauth_ctx_t *ctx, const char *service, const char **files, const char **extraparam, dt_oauth_reply_callback_t callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);

  GTree *params = g_tree_new_full((GCompareDataFunc)g_strcmp0, NULL, g_free, g_free);
  char *url = g_strconcat(ctx->endpoint, service, NULL);

  char *baseurl = dt_oauth_sign_url(ctx, url, extraparam, "POST", params);
  int ret = dt_curl_query_post(ctx, baseurl, params, files, callback, callbackdata);
  g_free(url);
  g_free(baseurl);
  g_tree_destroy(params);
  return ret;
}

int dt_oauth_request_token(dt_oauth_ctx_t *ctx, const char *method, const char *service, const char **extraparam, dt_oauth_param_callback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  subcallbackdata_t subcallback = { callback, callbackdata };
  subcallbackdata_t update_oauth_info_cb = { (dt_oauth_param_callback)dt_oauth_update_ctx_token_cb, &subcallback};
  if (method == NULL || g_strcmp0(method, "GET") == 0)
    return dt_oauth_get(ctx, service, extraparam, (dt_oauth_reply_callback_t)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
  else
    return dt_oauth_post(ctx, service, extraparam, (dt_oauth_reply_callback_t)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
}

int dt_oauth_access_token(dt_oauth_ctx_t *ctx, const char *method, const char *service, const char **extraparam, dt_oauth_param_callback callback, gpointer callbackdata)
{
  g_return_val_if_fail(ctx != NULL, DT_OAUTH_NO_VALID_CONTEXT);
  subcallbackdata_t subcallback = { callback, callbackdata };
  subcallbackdata_t update_oauth_info_cb = { (dt_oauth_param_callback)dt_oauth_update_ctx_token_cb, &subcallback};
  if (method == NULL || g_strcmp0(method, "GET") == 0)
    return dt_oauth_get(ctx, service, extraparam, (dt_oauth_reply_callback_t)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
  else
    return dt_oauth_post(ctx, service, extraparam, (dt_oauth_reply_callback_t)dt_oauth_parse_urlencoded_reply, &update_oauth_info_cb);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
