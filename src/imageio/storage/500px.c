/*
    This file is part of darktable,
    copyright (c) 2010-2011 Jose Carlos Garcia Sogo

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

#include "dtgtk/button.h"
#include "dtgtk/label.h"
#include "gui/gtk.h"
#include "common/darktable.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio_module.h"
#include "common/imageio.h"
#include "common/tags.h"
#include "common/pwstorage/pwstorage.h"
#include "common/metadata.h"
#include "common/oauth1.h"
#include "control/conf.h"
#include "control/control.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>

DT_MODULE(1)

#define CONSUMER_KEY "ZPyvWbNCiiUvJp1SX2aD3Z6wxEjjOqhKzYzzmuFU"
#define CONSUMER_SECRET "VDShqWQE37TR38bTAZSzU1K8FMGoSP311xopYAQ8"

typedef struct _px500_api_context_t
{
  dt_oauth_ctx_t *fc;

  gchar *photo_id;
  gchar *upload_key;

  gboolean needsReauthentication;

  /** Current album used when posting images... */
  //flickcurl_photoset  *current_album;

  char *album_title;
  char *album_summary;
  int album_public;
  gboolean new_album;
  gboolean error_occured;

} _px500_api_context_t;

typedef struct dt_storage_px500_gui_data_t
{

  GtkLabel *label1,*label2,*label3, *label4,*label5,*label6,*label7,*labelPerms;    // username, password, albums, status, albumtitle, albumsummary, albumrights
  GtkEntry *entry2,*entry3,*entry4;                             // username, password, albumtitle,albumsummary
  GtkComboBox *comboBox1;                                               // album box
  GtkCheckButton *checkButton2;                                         // export tags
  GtkDarktableButton *dtbutton1;                                        // refresh albums
  GtkButton *button;                                                    // login button. These buttons call the same functions
  GtkBox *hbox1;                                                        // Create album options...
  GtkComboBox *permsComboBox;                                           // Permissions for flickr

  char *user_token;

  /* List of albums */
  //flickcurl_photoset **albums;

  /** Current px500 context for the gui */
  _px500_api_context_t *px500_api;

} dt_storage_px500_gui_data_t;


typedef struct dt_storage_px500_params_t
{
  int64_t hash;
  _px500_api_context_t *px500_api;
  gboolean export_tags;
  gboolean public_perm;
  gboolean friend_perm;
  gboolean family_perm;
} dt_storage_px500_params_t;


void static set_logged(dt_storage_px500_gui_data_t *ui, gboolean logged);

/** Authenticates and retreives an initialized px500 api object */
static dt_oauth_ctx_t *_px500_api_authenticate(dt_storage_px500_gui_data_t *ui);

//px500_upload_status static *_px500_api_upload_photo(dt_storage_px500_params_t *params, char *data, char *caption, char *description, gint imgid);

void static _px500_api_free( _px500_api_context_t *ctx )
{

  g_free( ctx->album_title );
  g_free( ctx->album_summary );

  //if (ctx->current_album != NULL)
    //flickcurl_free_photoset (ctx->current_album);

  //if (ctx->fc != NULL)
    //flickcurl_free (ctx->fc);

 // g_free( ctx );
}

#if 0
static void _px500_api_error_handler(void *data, const char *message)
{
  dt_control_log(_("px500: %s"), message);
  printf("px500: %s\n", message);
  if (data)
  {
    _px500_api_context_t *ctx = (_px500_api_context_t *)data;
    ctx->error_occured = 1;
  }
}
#endif

static dt_oauth_ctx_t *_px500_api_authenticate(dt_storage_px500_gui_data_t *ui)
{
  gchar *username, *access_token, *access_token_secret;
  gint rc;

  _px500_api_context_t *ctx = ui->px500_api;

  /* FIXME: A minor memleak that avoid double-free problems.
  if (ctx->fc != NULL)
    flickcurl_free(ctx->fc);
  */

  ctx->fc = dt_oauth_ctx_init ("https://api.500px.com/v1/", CONSUMER_KEY, CONSUMER_SECRET);
  
  // Retrieve stored auth_key
  GHashTable* table = dt_pwstorage_get("px500");
  username = g_strdup (g_hash_table_lookup(table, "username"));
  //user_nsid = g_strdup (g_hash_table_lookup(table, "user_nsid"));
  access_token = g_strdup (g_hash_table_lookup(table, "oauth_token"));
  access_token_secret = g_strdup (g_hash_table_lookup(table, "oauth_token_secret"));
  g_hash_table_destroy(table);

  if (username != NULL && ctx->needsReauthentication != TRUE)
    ui->user_token = g_strdup(username);

  if (access_token == NULL || access_token_secret == NULL || username == NULL || ctx->needsReauthentication == TRUE)
  {
    gchar *username, *password;

    //get text from entries
    username = g_strdup("jcsogo@gmail.com");
    password = g_strdup("Randomize");

    const char* parms[] = {
       "oauth_callback", "oob",
       NULL };

    rc = dt_oauth_request_token (ctx->fc, "GET", "oauth/request_token", parms, NULL, NULL);

    if (rc)
    {
      set_logged (ui, FALSE);
      ui->user_token = NULL;
      dt_oauth_ctx_destroy (ctx->fc);
      return NULL;
    }

    access_token = dt_oauth_get_token (ctx->fc);

    const char *baseurl = dt_oauth_get_authorize_uri(ctx->fc);
    
    ////////////// build & show the validation dialog
    gchar *text1 = _("step 1: a new window or tab of your browser should have been "
                     "loaded. you have to login into your facebook account there "
                     "and authorize darktable to upload photos before continuing.");
    gchar *text2 = _("step 2: paste your browser url and click the ok button once "
                     "you are done.");

    GtkWidget *window = dt_ui_main_window(darktable.gui->ui);
    GtkDialog *fb_auth_dialog = GTK_DIALOG(gtk_message_dialog_new (GTK_WINDOW (window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_OK_CANCEL,
                                    _("Facebook authentication")));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (fb_auth_dialog),
        "%s\n\n%s", text1, text2);

    GtkWidget *entry = gtk_entry_new();
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(gtk_label_new(_("url:"))), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(entry), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(fb_auth_dialog->vbox), hbox, TRUE, TRUE, 0);

    gtk_widget_show_all(GTK_WIDGET(fb_auth_dialog));

    ////////////// wait for the user to entrer the validation URL
    gint result;
    gchar *token = NULL;
    const char *replyurl;
    while (TRUE)
    {
      result = gtk_dialog_run (GTK_DIALOG (fb_auth_dialog));
      if (result == GTK_RESPONSE_CANCEL)
        break;
      replyurl = gtk_entry_get_text(GTK_ENTRY(entry));
      if (replyurl == NULL || g_strcmp0(replyurl, "") == 0)
      {
        gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(fb_auth_dialog),
             "%s\n\n%s\n\n<span foreground=\"" MSGCOLOR_RED "\" ><small>%s</small></span>",
             text1, text2, _("please enter the validation url"));
        continue;
      }
    }

    char* *urlchunks = g_strsplit_set(url, "#&=", -1);
    //starts at 1 to skip the url prefix, then values are in the form key=value
    for (int i = 1; urlchunks[i] != NULL; i += 2)
    {
      if ((g_strcmp0(urlchunks[i], "verifier") == 0) && (urlchunks[i + 1] != NULL))
      {
        verifier = g_strdup(urlchunks[i + 1]);
        break;
      }
    }
    if (verifier != NULL) //we have a valid verifier
      break;
    else
      gtk_message_dialog_format_secondary_markup(
            GTK_MESSAGE_DIALOG(fb_auth_dialog),
            "%s\n\n%s%s\n\n<span foreground=\"" MSGCOLOR_RED "\"><small>%s</small></span>",
            text1, text2,
            _("the given url is not valid, it should look like: "),
              FB_WS_BASE_URL"connect/login_success.html?...");
    
    gtk_widget_destroy(GTK_WIDGET(fb_auth_dialog));
    
/*
    const char* params[] = {
        "x_auth_mode", "client_auth",
        "x_auth_username", username,
        "x_auth_password", password,
        "oauth_callback", "oob",
        NULL};
  */  
    const char* params[] = {
        "oauth_token", access_token,
        "oauth_callback", "http://www.darktable.org/",
        "oauth_verifier", verifier
        NULL};
    rc = dt_oauth_access_token(ctx->fc, "GET", "oauth/access_token", params, NULL, NULL);
    printf ("500px: access token called. Return %d\n", rc);

    if (!rc)
    {
      access_token = dt_oauth_get_token (ctx->fc);
      access_token_secret = dt_oauth_get_token_secret (ctx->fc);
      ui->user_token = g_strdup(username);

      /* Add creds to pwstorage */
      GHashTable *table = g_hash_table_new(g_str_hash, g_str_equal);

      g_hash_table_insert(table, "oauth_token", access_token);
      g_hash_table_insert(table, "oauth_token_secret", access_token_secret);
      g_hash_table_insert(table, "username", username);
      //g_hash_table_insert(table, "user_nsid", user_nsid);

      if( !dt_pwstorage_set("px500", table) )
        dt_print(DT_DEBUG_PWSTORAGE,"[px500] cannot store user/token\n");

      g_free(access_token);
      g_free(access_token_secret);
      g_free(username);
      g_hash_table_destroy(table);

      set_logged (ui, TRUE); 
      return ctx->fc;
    }
    else
    {
      set_logged (ui, FALSE);
      ui->user_token = NULL;
      _px500_api_free(ctx);
      return NULL;
    }
  }
  else
  {
    dt_oauth_set_token (ctx->fc, access_token, access_token_secret);

    set_logged (ui, TRUE);
    return ctx->fc;
  }
}

#if 0
px500_upload_status static *_px500_api_upload_photo( dt_storage_px500_params_t *p, char *fname, char *caption, char *description, gint imgid )
{
  px500_upload_params *params = g_malloc(sizeof(px500_upload_params));
  px500_upload_status *status;

  memset(params,0,sizeof(px500_upload_params));
  params->safety_level = 1; //Defaults to safe photos
  params->content_type = 1; //Defaults to photo (we don't support video!)

  params->title = caption;
  params->description = description;

  if (imgid)
    params->tags = dt_tag_get_list(imgid, ",");
  params->photo_file = fname; //fname should be the URI of temp file

  params->is_public = (int) p->public_perm;
  params->is_friend = (int) p->friend_perm;
  params->is_family = (int) p->family_perm;

  status = px500_photos_upload_params(p->px500_api->fc, params);
  if (!status)
  {
    fprintf (stderr,"[px500] Something went wrong when uploading");
    g_free (params);
    return NULL;
  }
  g_free(params);
  return status;
}


char static *_px500_api_create_photoset(_px500_api_context_t *ctx, const char *photo_id )
{
  char *photoset;
  const char *title = ctx->album_title;
  const char *summary = ctx->album_summary;

  photoset = px500_photosets_create (ctx->fc, title, summary, photo_id, NULL);
  if (!photoset)
    fprintf(stderr,"[px500] Something went wrong when creating gallery %s", title);
  return photoset;
}
#endif

const char*
name ()
{
  return _("px500 webalbum");
}

/** Set status connection text */
/*
void static set_status(dt_storage_flickr_gui_data_t *ui, gchar *message, gchar *color)
{
  if( !color ) color="#ffffff";
  gchar mup[512]= {0};
  sprintf( mup,"<span foreground=\"%s\" ><small>%s</small></span>",color,message);
  gtk_label_set_markup(ui->label4, mup);
}
*/

void static set_logged(dt_storage_px500_gui_data_t *ui, gboolean logged)
{
  if (logged == FALSE || ui->user_token == NULL)
  {
    gtk_label_set_text (GTK_LABEL(ui->label1), _("not logged in"));
    gtk_button_set_label (GTK_BUTTON(ui->button), _("login"));
    gtk_widget_set_sensitive(GTK_WIDGET( ui->comboBox1 ), FALSE);
  }
  else
  {
    gchar *text = NULL;

    text = dt_util_dstrcat (text, _("logged in as %s"), ui->user_token);
    // TODO: Use ellipsization and max_width when using fullname
    gtk_label_set_text (GTK_LABEL(ui->label1), text);
    
    gtk_button_set_label (GTK_BUTTON(ui->button), _("logout"));
  }
}
/*
void static flickr_entry_changed(GtkEntry *entry, gpointer data)
{
  dt_storage_flickr_gui_data_t *ui=(dt_storage_flickr_gui_data_t *)data;

  if( ui->flickr_api != NULL)
  {
    ui->flickr_api->needsReauthentication=TRUE;
    if (ui->user_token)
    {
      g_free(ui->user_token);
      ui->user_token = NULL;
    }
    set_status(ui,_("not authenticated"), "#e07f7f");
    gtk_widget_set_sensitive(GTK_WIDGET( ui->comboBox1 ) ,FALSE);
  }
}
*/

#if 0
px500_photoset static **_px500_api_photosets( _px500_api_context_t *ctx, const char *user)
{
  flickcurl_photoset **photoset;
  photoset = flickcurl_photosets_getList(ctx->fc, NULL);

  return photoset;
}


/** Refresh albums */
void static refresh_albums(dt_storage_px500_gui_data_t *ui)
{
  int i;
  gtk_widget_set_sensitive( GTK_WIDGET(ui->comboBox1), FALSE);

  if (ui->px500_api->fc == NULL || ui->px500_api->needsReauthentication == TRUE)
  {
    ui->px500_api->fc = _px500_api_authenticate(ui);
    if (ui->px500_api->fc == NULL)
    {
      set_logged(ui, FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET( ui->comboBox1 ), FALSE);
      return;
    }
  }

  // First clear the model of data except first item (Create new album)
  GtkTreeModel *model=gtk_combo_box_get_model(ui->comboBox1);
  gtk_list_store_clear (GTK_LIST_STORE(model));

  ui->albums = _px500_api_photosets(ui->px500_api, NULL);
  if( ui->albums )
  {

    // Add standard action
    gtk_combo_box_append_text( ui->comboBox1, _("without album") );
    gtk_combo_box_append_text( ui->comboBox1, _("create new album") );
    gtk_combo_box_append_text( ui->comboBox1, "" );// Separator

    // Then add albums from list...
    for(i=0; ui->albums[i]; i++)
    {
      char data[512]= {0};
      sprintf(data,"%s (%i)", ui->albums[i]->title, ui->albums[i]->photos_count);
      gtk_combo_box_append_text( ui->comboBox1, g_strdup(data));
    }
    gtk_combo_box_set_active( ui->comboBox1, 3);
    gtk_widget_hide( GTK_WIDGET(ui->hbox1) ); // Hide create album box...
  }
  else
  {
    // Failed to parse feed of album...
    // Lets notify somehow...
    gtk_combo_box_set_active( ui->comboBox1, 0);
  }
  gtk_widget_set_sensitive( GTK_WIDGET(ui->comboBox1), TRUE);

}


void static px500_album_changed(GtkComboBox *cb,gpointer data)
{
  dt_storage_px500_gui_data_t * ui=(dt_storage_px500_gui_data_t *)data;
  gchar *value=gtk_combo_box_get_active_text(ui->comboBox1);
  if( value!=NULL && strcmp( value, _("create new album") ) == 0 )
  {
    gtk_widget_set_no_show_all(GTK_WIDGET(ui->hbox1), FALSE);
    gtk_widget_show_all(GTK_WIDGET(ui->hbox1));
  }
  else
    gtk_widget_hide(GTK_WIDGET(ui->hbox1));
}
#endif

gboolean static combobox_separator(GtkTreeModel *model,GtkTreeIter *iter,gpointer data)
{
  GValue value = { 0, };
  gtk_tree_model_get_value(model,iter,0,&value);
  gchar *v=NULL;
  if (G_VALUE_HOLDS_STRING (&value))
  {
    if( (v=(gchar *)g_value_get_string (&value))!=NULL && strlen(v) == 0 ) return TRUE;
  }
  return FALSE;
}

#if 0
// Refresh button pressed...
void static px500_refresh_clicked(GtkButton *button,gpointer data)
{
  dt_storage_px500_gui_data_t * ui=(dt_storage_px500_gui_data_t *)data;
   
  refresh_albums(ui);
}
#endif

void static px500_button1_clicked(GtkButton *button,gpointer data)
{
  dt_storage_px500_gui_data_t * ui=(dt_storage_px500_gui_data_t *)data;
  if (ui->user_token != NULL)
  {
    ui->px500_api->needsReauthentication = TRUE;
    set_logged(ui, FALSE);
    g_free(ui->user_token);
    ui->user_token = NULL;
    return;
  }
   
  //refresh_albums(ui);
  _px500_api_authenticate(ui);
}

/*
static gboolean
focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  dt_control_tab_shortcut_off(darktable.control);
  return FALSE;
}

static gboolean
focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  dt_control_tab_shortcut_on(darktable.control);
  return FALSE;
}
*/

void
gui_init (dt_imageio_module_storage_t *self)
{
  self->gui_data = (dt_storage_px500_gui_data_t *)g_malloc(sizeof(dt_storage_px500_gui_data_t));
  memset(self->gui_data,0,sizeof(dt_storage_px500_gui_data_t));
  dt_storage_px500_gui_data_t *ui= self->gui_data;
  
  ui->px500_api = (_px500_api_context_t *)g_malloc(sizeof(_px500_api_context_t));
  memset(ui->px500_api,0,sizeof(_px500_api_context_t));

 // flickcurl_init ();

  self->widget = gtk_vbox_new(FALSE, 0);

  GtkWidget *hbox1=gtk_hbox_new(FALSE,5);
  GtkWidget *hbox0=gtk_hbox_new(FALSE,5);
  GtkWidget *vbox1=gtk_vbox_new(FALSE,0);
  GtkWidget *vbox2=gtk_vbox_new(FALSE,5);

  ui->label1 = GTK_LABEL(  gtk_label_new( _("not logged in") ) );
  ui->label3 = GTK_LABEL(  gtk_label_new( _("photosets") ) );
  ui->labelPerms = GTK_LABEL(  gtk_label_new( _("visible to") ) );
  //ui->label4 = GTK_LABEL(  gtk_label_new( NULL ) );

  //set_status(ui,_("click login button to start"), "#ffffff");

  ui->label5 = GTK_LABEL(  gtk_label_new( _("title") ) );
  ui->label6 = GTK_LABEL(  gtk_label_new( _("summary") ) );
  gtk_misc_set_alignment(GTK_MISC(ui->label1),      0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(ui->labelPerms),  0.0, 0.9);
  gtk_misc_set_alignment(GTK_MISC(ui->label3),      0.0, 0.7);
  gtk_misc_set_alignment(GTK_MISC(ui->label5),      0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(ui->label6),      0.0, 0.5);

//  ui->entry1 = GTK_ENTRY( gtk_entry_new() );
  ui->entry3 = GTK_ENTRY( gtk_entry_new() );  // Album title
  ui->entry4 = GTK_ENTRY( gtk_entry_new() );  // Album summary

//  dt_gui_key_accel_block_on_focus (GTK_WIDGET (ui->entry1));
  dt_gui_key_accel_block_on_focus (GTK_WIDGET (ui->entry3));
  dt_gui_key_accel_block_on_focus (GTK_WIDGET (ui->entry4));

  gtk_entry_set_text( ui->entry3, _("my new photoset") );
  gtk_entry_set_text( ui->entry4, _("exported from darktable") );

  GtkWidget *albumlist=gtk_hbox_new(FALSE,0);
  ui->comboBox1=GTK_COMBO_BOX( gtk_combo_box_new_text()); // Available albums

  dt_ellipsize_combo(ui->comboBox1);

  ui->dtbutton1 = DTGTK_BUTTON( dtgtk_button_new(dtgtk_cairo_paint_refresh,0) );
  g_object_set(G_OBJECT(ui->dtbutton1), "tooltip-text", _("refresh album list"), (char *)NULL);
  
  ui->button = GTK_BUTTON(gtk_button_new_with_label(_("login")));
  g_object_set(G_OBJECT(ui->button), "tooltip-text", _("px500 login"), (char *)NULL);

  gtk_widget_set_sensitive( GTK_WIDGET(ui->comboBox1), FALSE);
  gtk_combo_box_set_row_separator_func(ui->comboBox1,combobox_separator,ui->comboBox1,NULL);
  gtk_box_pack_start(GTK_BOX(albumlist), GTK_WIDGET(ui->comboBox1), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(albumlist), GTK_WIDGET(ui->dtbutton1), FALSE, FALSE, 0);

  ui->checkButton2 = GTK_CHECK_BUTTON( gtk_check_button_new_with_label(_("export tags")) );
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON( ui->checkButton2 ),TRUE);

  ui->permsComboBox = GTK_COMBO_BOX(gtk_combo_box_new_text());
  gtk_combo_box_append_text(ui->permsComboBox, _("you"));
  gtk_combo_box_append_text(ui->permsComboBox, _("friends"));
  gtk_combo_box_append_text(ui->permsComboBox, _("family"));
  gtk_combo_box_append_text(ui->permsComboBox, _("friends + family"));
  gtk_combo_box_append_text(ui->permsComboBox, _("everyone"));
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->permsComboBox), 0); // Set default permission to private

  gtk_box_pack_start(GTK_BOX(self->widget), hbox0, TRUE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox1, TRUE, FALSE, 0);
//  gtk_box_pack_start(GTK_BOX( hbox0 ), GTK_WIDGET( ui->entry1 ), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( hbox0 ), GTK_WIDGET( ui->button ), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( hbox0 ), GTK_WIDGET( ui->label1 ), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( hbox1 ), vbox1, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( hbox1 ), vbox2, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( vbox1 ), GTK_WIDGET( gtk_label_new("")), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( vbox1 ), GTK_WIDGET( ui->labelPerms ), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( vbox1 ), GTK_WIDGET( ui->label3 ), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( ui->label4 ), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( ui->checkButton2 ), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( ui->permsComboBox ), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( albumlist ), TRUE, FALSE, 0);


  // Create Album
  ui->hbox1=GTK_BOX(gtk_hbox_new(FALSE,5));
  gtk_widget_set_no_show_all(GTK_WIDGET(ui->hbox1), TRUE);
  vbox1=gtk_vbox_new(FALSE,0);
  vbox2=gtk_vbox_new(FALSE,0);

  gtk_box_pack_start(GTK_BOX(ui->hbox1), vbox1, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ui->hbox1), vbox2, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(ui->hbox1), TRUE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX( vbox1 ), GTK_WIDGET( ui->label5 ), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX( vbox1 ), GTK_WIDGET( ui->label6 ), TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( ui->entry3 ), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX( vbox2 ), GTK_WIDGET( ui->entry4 ), TRUE, FALSE, 0);

  // Setup signals
  // add signal on realize and hide gtk_widget_hide(GTK_WIDGET(ui->hbox1));

//  g_signal_connect(G_OBJECT(ui->dtbutton1), "clicked", G_CALLBACK(px500_refresh_clicked), (gpointer)ui);
  g_signal_connect(G_OBJECT(ui->button), "clicked", G_CALLBACK(px500_button1_clicked), (gpointer)ui);
//  g_signal_connect(G_OBJECT(ui->entry1), "changed", G_CALLBACK(px500_entry_changed), (gpointer)ui);
//  g_signal_connect(G_OBJECT(ui->comboBox1), "changed", G_CALLBACK(px500_album_changed), (gpointer)ui);

  /**
  dont' populate the combo on startup, save 3 second

  // If username and password is stored, let's populate the combo
  if( _username && _password )
  {
    ui->user_token = _password;
    refresh_albums(ui);
  }
  */

//  if( _username )
//    g_free (_username);
  gtk_combo_box_set_active( ui->comboBox1, FALSE);
}

void
gui_cleanup (dt_imageio_module_storage_t *self)
{
        g_free(self->gui_data);
}

void
gui_reset (dt_imageio_module_storage_t *self)
{
}

static int px500_parse_photocreate(_px500_api_context_t * ctx, long int code, const char* reply, gpointer data)
{
  GError *error;
  JsonParser *json_parser = json_parser_new();
  json_parser_load_from_data(json_parser, reply, strlen(reply), &error);
  JsonNode *root = json_parser_get_root(json_parser);
  JsonObject* rootdict = json_node_get_object(root);
  const char* upload_key = json_object_get_string_member(rootdict, "upload_key");
  printf("upload key : %s\n", upload_key);
  ctx->upload_key = strdup(upload_key);
  JsonObject* photodict = json_object_get_object_member(rootdict, "photo");
  int photoid = json_object_get_int_member(photodict, "id");
  printf("photo id : %d\n", photoid);
  ctx->photo_id = g_strdup_printf("%d", photoid);;
  g_object_unref(json_parser);
  return 0;
}


int
store (dt_imageio_module_data_t *sdata, const int imgid, dt_imageio_module_format_t *format, dt_imageio_module_data_t *fdata,
       const int num, const int total, const gboolean high_quality)
{
  gint result=1;
  dt_storage_px500_params_t *p=(dt_storage_px500_params_t *)sdata;
  _px500_api_context_t *ctx = p->px500_api;
  //flickcurl_upload_status *photo_status;
  //gint tags=0;

  const char *ext = format->extension(fdata);

  // Let's upload image...

  /* construct a temporary file name */
  char fname[4096]= {0};
  dt_loc_get_tmp_dir (fname,4096);
  g_strlcat (fname,"/darktable.XXXXXX.",4096);
  g_strlcat(fname,ext,4096);

  char *caption = NULL;
  //char *description = NULL;


  gint fd=g_mkstemp(fname);
  fprintf(stderr,"tempfile: %s\n",fname);
  if(fd==-1)
  {
    dt_control_log("failed to create temporary image for px500 export");
    return 1;
  }
  close(fd);
  const dt_image_t *img = dt_image_cache_read_get(darktable.image_cache, imgid);
  caption = g_path_get_basename( img->filename );

  // If title is not existing, then use the filename without extension. If not, then use title instead
  GList *title = dt_metadata_get(img->id, "Xmp.dc.title", NULL);
  if(title != NULL)
  {
    caption = title->data;
  }
  else
  {
    (g_strrstr(caption,"."))[0]='\0'; // Shop extension...
  }

  GList *desc = dt_metadata_get(img->id, "Xmp.dc.description", NULL);
  //if(desc != NULL)
  //{
  //  description = desc->data;
  //}
  dt_image_cache_read_release(darktable.image_cache, img);

  if(dt_imageio_export(imgid, fname, format, fdata, high_quality) != 0)
  {
    fprintf(stderr, "[imageio_storage_px500] could not export to file: `%s'!\n", fname);
    dt_control_log(_("could not export to file `%s'!"), fname);
    result = 0;
    goto cleanup;
  }

#ifdef _OPENMP
  #pragma omp critical
#endif
  {
    //TODO: Check if this could be done in threads, so we enhace export time by using
    //      upload time for one image to export another image to disk.
    // Upload image
    // Do we export tags?
    //if( p->export_tags == TRUE )
    //  tags = imgid;
    //photo_status = _px500_api_upload_photo( p, fname, caption, description, tags );
//    const char *fnames [] = { fname, NULL };
//    dt_oauth_post_files (p->px500_api->fc, "upload", fnames, NULL, NULL, NULL);
    const char* newphotoparams[] = {
      "name", caption,
      "description", "test description",
      "category", 0,
      NULL};
    dt_oauth_post(ctx->fc, "photos", newphotoparams, (dt_oauth_reply_callback_t)px500_parse_photocreate, NULL);

    const char* photoupload[] = {
      "upload_key", ctx->upload_key,
      "consumer_key", ctx->fc->consumer_key,
      "access_key", ctx->fc->token_key,
      "photo_id", ctx->photo_id,
      NULL };

    const char* photoupload_files[] = {
      "file", img->filename,
      NULL };
      
      dt_oauth_post_files(ctx->fc, "upload", photoupload_files, photoupload, NULL, NULL); 

  }

  //if( !photo_status )
  //{
  //  result=0;
  //  goto cleanup;
  //}


  // FIXME: Create or add to photosets
cleanup:

  // And remove from filesystem..
  unlink( fname );
  g_free( caption );
  if(desc)
  {
    g_free(desc->data);
    g_list_free(desc);
  }

  if (result)
  {
    //this makes sense only if the export was successful
    dt_control_log(_("%d/%d exported to px500 webalbum"), num, total );
  }
  return result;
}

void*
get_params(dt_imageio_module_storage_t *self, int *size)
{
  // have to return the size of the struct to store (i.e. without all the variable pointers at the end)
  // TODO: if a hash to encrypted data is stored here, return only this size and store it at the beginning of the struct!
  *size = sizeof(int64_t);
  dt_storage_px500_gui_data_t *ui =(dt_storage_px500_gui_data_t *)self->gui_data;
  dt_storage_px500_params_t *d = (dt_storage_px500_params_t *)g_malloc(sizeof(dt_storage_px500_params_t));
  if(!d) return NULL;
  memset(d,0,sizeof(dt_storage_px500_params_t));
  d->px500_api = (_px500_api_context_t *)g_malloc(sizeof(_px500_api_context_t));
  memset(d->px500_api,0,sizeof(_px500_api_context_t));
  d->hash = 1;

  // fill d from controls in ui
  if( ui->px500_api && ui->px500_api->needsReauthentication == FALSE)
  {
    // We are authenticated and off to actually export images..
    d->px500_api = ui->px500_api;
    int index = gtk_combo_box_get_active(ui->comboBox1);
    if( index >= 0 )
    {
      switch(index)
      {
        case 0: // No album
          //d->px500_api->current_album = NULL;
          break;
        case 1: // Create new album
          //d->px500_api->current_album = NULL;
          d->px500_api->album_title = g_strdup( gtk_entry_get_text( ui->entry3 ) );
          d->px500_api->album_summary = g_strdup( gtk_entry_get_text( ui->entry4) );
          d->px500_api->new_album = TRUE;
          break;
        default:
          // use existing album
          //d->px500_api->current_album = flickcurl_photosets_getInfo(d->px500_api->fc,ui->albums[index-3]->id);
          //if( d->px500_api->current_album == NULL )
          //{
            // Something went wrong...
            //fprintf(stderr,"Something went wrong.. album index %d = NULL\n",index-3 );
            //g_free(d);
            //return NULL;
          //}
          break;
      }

    }
    else
    {
      g_free(d);
      return NULL;
    }

    d->export_tags = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->checkButton2));

    /* Handle the permissions */
    int perm_index = (int) gtk_combo_box_get_active(GTK_COMBO_BOX(ui->permsComboBox));
    switch(perm_index)
    {
      case 0: // Private
        d->public_perm = 0;
        d->friend_perm = 0;
        d->family_perm = 0;
        break;
      case 1: // Friends
        d->public_perm = 0;
        d->friend_perm = 1;
        d->family_perm = 0;
        break;
      case 2: // Family
        d->public_perm = 0;
        d->friend_perm = 0;
        d->family_perm = 1;
        break;
      case 3: // Friend + Family
        d->public_perm = 0;
        d->friend_perm = 1;
        d->family_perm = 1;
        break;
      case 4: //Public
        d->public_perm = 1;
        d->friend_perm = 0;
        d->family_perm = 0;
        break;
    }

    // Let UI forget about this api context and recreate a new one for further usage...
    // FIXME Why?
    //d->px500_api->fc = ui->px500_api->fc = _px500_api_authenticate(ui);
    if (!ui->px500_api)
      set_logged (ui, FALSE);
  }
  else
  {
    //set_status(ui,_("not authenticated"), "#e07f7f");
    set_logged(ui, FALSE);
    dt_control_log(_("px500 account not authenticated"));
    g_free(d);
    return NULL;
  }
  return d;
}

int
set_params(dt_imageio_module_format_t *self, void *params, int size)
{
  if(size != sizeof(int64_t)) return 1;
  // gui stuff not updated, as sensitive user data is not stored in the preset.
  // TODO: store name/hash in kwallet/etc module and get encrypted stuff from there!
  return 0;
}

int supported(dt_imageio_module_storage_t *storage, dt_imageio_module_format_t *format)
{
  if( strcmp(format->mime(NULL) ,"image/jpeg") ==  0 ) return 1;
  else if( strcmp(format->mime(NULL) ,"image/png") ==  0 ) return 1;

  return 0;
}

void
free_params(dt_imageio_module_storage_t *self, void *params)
{
  dt_storage_px500_params_t *d = (dt_storage_px500_params_t *)params;

  _px500_api_free (d->px500_api); //TODO

  free(params);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
