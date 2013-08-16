/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.
    copyright (c) 2013 Jose Carlos Garcia Sogo

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

#include "common/darktable.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "dtgtk/button.h"
#include "libs/lib.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"

DT_MODULE(1)

#define DT_LIB_SNAPSHOTS_COUNT 4

#define HANDLE_SIZE 0.02

/* a snapshot */
typedef struct dt_lib_snapshot_t
{
  GtkWidget *button;
  float zoom_x, zoom_y, zoom_scale;
  int32_t zoom, closeup;
  char filename[512];
}
dt_lib_snapshot_t;


typedef struct dt_lib_snapshots_t
{
  GtkWidget *snapshots_box;

  int selected;

  /* current active snapshots */
  int num_snapshots;

  /* size of snapshots */
  int size;

  /* snapshots */
  //dt_lib_snapshot_t *snapshot;
  GList *snapshot;

  /* snapshot cairo surface */
  cairo_surface_t *snapshot_image;


  /* change snapshot overlay controls */
  gboolean dragging,vertical,inverted;
  double vp_width,vp_height,vp_xpointer,vp_ypointer;

  GtkWidget *take_button;
  GtkWidget *split_button;

  /* Overlay or split mode? */
  gboolean split;
  
  /* Cache for bufer... FIXME: should they be here or in dt_snapshot_t?*/
  uint8_t *image_backbuf, *snapshot_backbuf;
  int image_backbuf_size, snapshot_backbuf_size;

  /* Mutex for expose */
  dt_pthread_mutex_t *snapshot_expose;

}
dt_lib_snapshots_t;

/* callback for take snapshot */
static void _lib_snapshots_add_button_clicked_callback (GtkWidget *widget, gpointer user_data);
static void _lib_snapshots_toggled_callback(GtkToggleButton *widget, gpointer user_data);
static void _lib_snapshots_split_button_toggled_callback (GtkWidget *widget, gpointer user_data);


const char* name()
{
  return _("snapshots");
}

uint32_t views()
{
  return DT_VIEW_DARKROOM;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 1000;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "take snapshot"), 0, 0);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t*)self->data;

  dt_accel_connect_button_lib(self, "take snapshot", d->take_button);
}

/* expose snapshot over center viewport */
void gui_post_expose(dt_lib_module_t *self, cairo_t *cri, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  
  // switch to know what are we processing in each expose event
  //static int process_snapshot = 0;
  static int imagen = 0;
  static int snap = 0;
  static int timestamp = 0;
  
  printf("SNAPSHOTS: post expose called - selected: %d\n", d->selected);

  // FIXME: check we are starting to count in 1 everywhere
  if (d->selected < 1) 
  {
    imagen = snap = 0;
    return;
  }

  // convert to image coordinates:
  double x_start = width  > darktable.thumbnail_width  ? (width - darktable.thumbnail_width) *.5f:0;
  double y_start = height > darktable.thumbnail_height ? (height- darktable.thumbnail_height)*.5f:0;

  dt_pthread_mutex_t *mutex = NULL;
  int wd, ht, stride, closeup;
  int32_t zoom;
  float zoom_x, zoom_y;
  DT_CTL_GET_GLOBAL(zoom_y, dev_zoom_y);
  DT_CTL_GET_GLOBAL(zoom_x, dev_zoom_x);
  DT_CTL_GET_GLOBAL(zoom, dev_zoom);
  DT_CTL_GET_GLOBAL(closeup, dev_closeup);
  
  static float roi_hash_old = -1.0f;
  // compute patented dreggn hash so we don't need to check all values:
  const float roi_hash = width + 7.0f*height + 23.0f*zoom + 42.0f*zoom_x + 91.0f*zoom_y + 666.0f*zoom;

  dt_develop_t *dev = darktable.develop;

  static cairo_surface_t *image_surface = NULL;
  static int image_surface_width = 0, image_surface_height = 0, image_surface_imgid = -1;
  if(image_surface_width != width || image_surface_height != height || image_surface == NULL)
  {
    // create double-buffered image to draw on, to make modules draw more fluently.
    image_surface_width = width;
    image_surface_height = height;
    if(image_surface) cairo_surface_destroy(image_surface);
    image_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    image_surface_imgid = -1; // invalidate old stuff
    snap = imagen = 0;
  }
  cairo_t *cr = cairo_create(image_surface);
  cairo_surface_t *surface, *surface_snapshot;
  
  wd = dev->pipe->backbuf_width;
  ht = dev->pipe->backbuf_height;
  
  printf("Height: %d, Width: %d\n Pipe: H %d - W %d\n", height, width, ht, wd);
  
  stride = cairo_format_stride_for_width (CAIRO_FORMAT_RGB24, wd);

  //process_snapshot++;
  //printf ("We are in the loop: state is %d\n", process_snapshot);
  printf ("Imagen: %d - Snapshot: %d - Dirty: %d\n", imagen, snap, dev->image_dirty);
  
  mutex = &dev->pipe->backbuf_mutex;

  //if((dev->image_dirty && process_snapshot == 1)|| dev->pipe->input_timestamp < dev->preview_pipe->input_timestamp)
  if (imagen == 0)
  {
    printf("Processing original image\n");
    dt_dev_process_image(dev);
    imagen = 1;
    return;
  }
  else if (imagen == 1 && !dev->image_dirty && dev->pipe->input_timestamp >= dev->preview_pipe->input_timestamp)
  {
    dt_pthread_mutex_lock(mutex);
    printf ("creating ORIGINAL image surface\n");
    //surface = cairo_image_surface_create_for_data (dev->pipe->backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
    d->image_backbuf_size = dev->pipe->backbuf_size;
    d->image_backbuf = malloc(sizeof(uint8_t)*d->image_backbuf_size);
    memcpy (d->image_backbuf, dev->pipe->backbuf, dev->pipe->backbuf_size);
    imagen = 2;
    timestamp = dev->pipe->input_timestamp;
    dt_pthread_mutex_unlock(mutex);
  }
  else if (imagen != 2 && dev->image_dirty)
    return;

  // Snapshot
  if(snap == 0 && !dev->image_dirty)
  {
    printf("Processing SNAPSHOT image\n");
    //dt_dev_clear_history_items(dev);
    if (dev->previous_history == NULL)
    {
      dev->previous_history = dev->history;
      dev->history = NULL;
    }
    dt_dev_read_snapshot_history(dev, d->selected);
    dt_dev_process_image(dev);
    snap = 1;
    return;
  }
  else if (snap == 1 && !dev->image_dirty && dev->pipe->input_timestamp > timestamp)
  {
    dt_pthread_mutex_lock(mutex);
    printf("creating SNAPSHOT image surface\n");
    d->snapshot_backbuf_size = dev->pipe->backbuf_size;
    d->snapshot_backbuf = malloc(sizeof(uint8_t)*d->snapshot_backbuf_size);
    memcpy (d->snapshot_backbuf, dev->pipe->backbuf, dev->pipe->backbuf_size);
    snap = 2;
    //surface_snapshot = cairo_image_surface_create_for_data (dev->pipe->backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
    dt_pthread_mutex_unlock(mutex);
  }
  else if (snap != 2 && dev->image_dirty)
    return;
 
  // I think we should check that we reach here being at 2 ... if not reset and restart. Or create this above.
  // Or just check here that the buffers exist and reset if they don't
  if (snap == 2 && d->image_backbuf != NULL && d->snapshot_backbuf != NULL)
  {
    surface = cairo_image_surface_create_for_data (d->image_backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
    surface_snapshot = cairo_image_surface_create_for_data (d->snapshot_backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
  }
  else
  {
    /* Something happened - Let's restart ??*/
    snap = 0;
    imagen = 0;
    d->image_backbuf = d->snapshot_backbuf = NULL;
    dt_control_queue_redraw_center();
    return;
  }
  
  // FIXME: This is only a snippet
  // The first part of the code can be shared with darktable.c
  // Draw center view
  printf ("Timestamps Pipe: %d -- Preview: %d - Timestamp: %d \n", dev->pipe->input_timestamp, dev->preview_pipe->input_timestamp, timestamp);
  if(!dev->image_dirty && dev->pipe->input_timestamp > timestamp)
  {
    roi_hash_old = roi_hash;
    dt_pthread_mutex_lock(mutex);

    cairo_save(cr);
    cairo_set_source_rgb (cr, .2, .2, .2);
    cairo_paint(cr);
    cairo_restore(cr);
    
    cairo_save(cr);
    cairo_translate(cr, .5f*(width-wd), .5f*(height-ht));
    cairo_rectangle(cr, 0, 0, .5f*wd, ht);
    cairo_clip_preserve(cr);
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb (cr, .3, .3, .3);
    cairo_stroke (cr);
    cairo_surface_destroy (surface);
    cairo_restore(cr);
    
    cairo_save(cr);
    //cairo_set_source_rgb (cr, .2, .2, .2);
    //cairo_paint(cr);
    cairo_translate(cr, .5f*(width-wd), .5f*(height-ht));
    cairo_rectangle(cr, .5f*wd, 0, .5f*wd, ht);
    cairo_clip_preserve(cr);
    cairo_set_source_surface (cr, surface_snapshot, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 10.0);
    cairo_set_source_rgb (cr, .7, .7, .7);
    cairo_stroke (cr);
    cairo_surface_destroy (surface_snapshot);
    cairo_restore(cr);

    dt_pthread_mutex_unlock(mutex);
    image_surface_imgid = dev->image_storage.id;
  }
  else if(!dev->preview_dirty && (roi_hash != roi_hash_old))
  {
    printf("snapshots: else_if\n");
  }

#if 0
  if(!dev->image_dirty && dev->pipe->input_timestamp >= dev->preview_pipe->input_timestamp)
  {
    // draw image
    roi_hash_old = roi_hash;
    mutex = &dev->pipe->backbuf_mutex;
    dt_pthread_mutex_lock(mutex);
    wd = dev->pipe->backbuf_width;
    ht = dev->pipe->backbuf_height;
    stride = cairo_format_stride_for_width (CAIRO_FORMAT_RGB24, wd);
    surface = cairo_image_surface_create_for_data (dev->pipe->backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
    //cairo_set_source_rgb (cr, .2, .2, .2);
    cairo_set_source_rgb (cr, .1, .8, .2);
    cairo_paint(cr);
    cairo_translate(cr, .5f*(width-wd), .5f*(height-ht));
#if 0
    if(closeup)
    {
      const float closeup_scale = 2.0;
      cairo_scale(cr, closeup_scale, closeup_scale);
      float boxw = 1, boxh = 1, zx0 = zoom_x, zy0 = zoom_y, zx1 = zoom_x, zy1 = zoom_y, zxm = -1.0, zym = -1.0;
      dt_dev_check_zoom_bounds(dev, &zx0, &zy0, zoom, 0, &boxw, &boxh);
      dt_dev_check_zoom_bounds(dev, &zx1, &zy1, zoom, 1, &boxw, &boxh);
      dt_dev_check_zoom_bounds(dev, &zxm, &zym, zoom, 1, &boxw, &boxh);
      const float fx = 1.0 - fmaxf(0.0, (zx0 - zx1)/(zx0 - zxm)), fy = 1.0 - fmaxf(0.0, (zy0 - zy1)/(zy0 - zym));
      cairo_translate(cr, -wd/(2.0*closeup_scale) * fx, -ht/(2.0*closeup_scale) * fy);
    }
#endif
    cairo_rectangle(cr, 0, 0, wd, ht);
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb (cr, .3, .3, .3);
    cairo_stroke(cr);
    cairo_surface_destroy (surface);
    dt_pthread_mutex_unlock(mutex);
    image_surface_imgid = dev->image_storage.id;
  }
  else
  {
    roi_hash_old = roi_hash;
    mutex = &dev->preview_pipe->backbuf_mutex;
    dt_pthread_mutex_lock(mutex);

    wd = dev->preview_pipe->backbuf_width;
    ht = dev->preview_pipe->backbuf_height;
    float zoom_scale = dt_dev_get_zoom_scale(dev, zoom, closeup ? 2 : 1, 1);
    cairo_set_source_rgb (cr, .2, .2, .2);
    cairo_paint(cr);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    stride = cairo_format_stride_for_width (CAIRO_FORMAT_RGB24, wd);
    surface = cairo_image_surface_create_for_data (dev->preview_pipe->backbuf, CAIRO_FORMAT_RGB24, wd, ht, stride);
    cairo_translate(cr, width/2.0, height/2.0f);
    cairo_scale(cr, zoom_scale, zoom_scale);
    cairo_translate(cr, -.5f*wd-zoom_x*wd, -.5f*ht-zoom_y*ht);
    // avoid to draw the 1px garbage that sometimes shows up in the preview :(
    cairo_rectangle(cr, 0, 0, wd-1, ht-1);
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_fill(cr);
    cairo_surface_destroy (surface);
    dt_pthread_mutex_unlock(mutex);
    image_surface_imgid = dev->image_storage.id;
  }
#endif

  cairo_save(cri);
  if(image_surface_imgid == dev->image_storage.id)
  {
    cairo_destroy(cr);
    cairo_set_source_surface(cri, image_surface, 0, 0);
    cairo_paint(cri);
  }
  cairo_restore(cri);

  if (roi_hash_old && closeup)
  {
    printf("Hello %d\n", image_surface_imgid);
  }

#if 0
  if(d->snapshot_image && d->split == TRUE)
  {
    d->vp_width = width;
    d->vp_height = height;

    /* check if mouse pointer is on draggable area */
    double xp = pointerx/d->vp_width;
    double yp = pointery/d->vp_height;
    double xpt = xp*0.01;
    double ypt = yp*0.01;
    gboolean mouse_over_control = d->vertical ? ((xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt)?TRUE:FALSE) :
                                    ((yp > d->vp_ypointer-ypt && yp < d->vp_ypointer+ypt)?TRUE:FALSE);

    /* set x,y,w,h of surface depending on split align and invert */
    double x = d->vertical ? (d->inverted?width*d->vp_xpointer:0) : 0;
    double y = d->vertical ? 0 : (d->inverted?height*d->vp_ypointer:0);
    double w = d->vertical ? (d->inverted?(width * (1.0 - d->vp_xpointer)):width * d->vp_xpointer) : width;
    double h = d->vertical ? height : (d->inverted?(height * (1.0 - d->vp_ypointer)):height * d->vp_ypointer);

    cairo_set_source_surface(cri, d->snapshot_image, x_start, y_start);
    //cairo_rectangle(cri, 0, 0, width*d->vp_xpointer, height);
    cairo_rectangle(cri,x,y,w,h);
    cairo_fill(cri);

    /* draw the split line */
    cairo_set_source_rgb(cri, .7, .7, .7);
    cairo_set_line_width(cri, (mouse_over_control ? 2.0 : 0.5) );

    if(d->vertical)
    {
      cairo_move_to(cri, width*d->vp_xpointer, 0.0f);
      cairo_line_to(cri, width*d->vp_xpointer, height);
    }
    else
    {
      cairo_move_to(cri, 0.0f,  height*d->vp_ypointer);
      cairo_line_to(cri, width, height*d->vp_ypointer);
    }
    cairo_stroke(cri);

    /* if mouse over control lets draw center rotate control, hide if split is dragged */
    if(!d->dragging && mouse_over_control)
    {
      cairo_set_line_width(cri,0.5);
      double s = width*HANDLE_SIZE;
      dtgtk_cairo_paint_refresh(cri,
                                (d->vertical ? width*d->vp_xpointer : width*0.5)-(s*0.5),
                                (d->vertical ? height*0.5 : height*d->vp_ypointer)-(s*0.5),
                                s,s,d->vertical?1:0);
    }
  }
  else
  {
    cairo_set_source_surface(cri, d->snapshot_image, 0, 0);
    cairo_rectangle(cri,0,0,width,height);
    cairo_fill(cri);
  }
#endif
}

int button_released(struct dt_lib_module_t *self, double x, double y, int which, uint32_t state)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  if(d->snapshot_image)
  {
    d->dragging = FALSE;
    return 1;
  }
  return 0;
}

static int _lib_snapshot_rotation_cnt = 0;

int button_pressed (struct dt_lib_module_t *self, double x, double y, double pressure, int which, int type, uint32_t state)
{
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  
  if(d->snapshot_image)
  {
    double xp = x/d->vp_width;
    double yp = y/d->vp_height;
    double xpt = xp*0.01;
    double ypt = yp*0.01;

    /* do the split rotating */
    double hhs = HANDLE_SIZE*0.5;
    if (which==1 && (
          ((d->vertical && xp > d->vp_xpointer-hhs && xp <  d->vp_xpointer+hhs) &&
           yp>0.5-hhs && yp<0.5+hhs) ||
          ((yp > d->vp_ypointer-hhs && yp < d->vp_ypointer+hhs) && xp>0.5-hhs && xp<0.5+hhs)
        ))
    {
      /* let's rotate */
      _lib_snapshot_rotation_cnt++;

      d->vertical = !d->vertical;
      if(_lib_snapshot_rotation_cnt%2)
        d->inverted = !d->inverted;

      d->vp_xpointer = xp;
      d->vp_ypointer = yp;
      dt_control_queue_redraw_center();
    }
    /* do the dragging !? */
    else if (which==1 &&
             (
               (d->vertical && xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt) ||
               (yp > d->vp_ypointer-ypt && yp < d->vp_ypointer+ypt)
             ))
    {
      d->dragging = TRUE;
      d->vp_ypointer = yp;
      d->vp_xpointer = xp;
      dt_control_queue_redraw_center();
    }
    return 1;
  }
  return 0;
}

int mouse_moved(dt_lib_module_t *self, double x, double y, double pressure, int which)
{
  dt_lib_snapshots_t *d=(dt_lib_snapshots_t *)self->data;

  if(d->snapshot_image)
  {
    double xp = x/d->vp_width;
    double yp = y/d->vp_height;
    //double xpt = xp*0.01;

    /* update x pointer */
    if(d->dragging)
    {
      d->vp_xpointer = xp;
      d->vp_ypointer = yp;
    }

    /* is mouse over control or in draggin state?, lets redraw */
    //    if(d->dragging || (xp > d->vp_xpointer-xpt && xp < d->vp_xpointer+xpt))
    dt_control_queue_redraw_center();

    return 1;
  }

  return 0;
}



void gui_reset(dt_lib_module_t *self)
{
  dt_lib_snapshots_t *d=(dt_lib_snapshots_t *)self->data;
  d->num_snapshots = 0;
  d->snapshot_image = NULL;

  //for(uint32_t k=0; k<d->size; k++)
  //  gtk_widget_hide(d->snapshot[k].button);
  // FIXME: what should we do in this case? It is not the same if we are changing the image or if user clicks reset

  dt_control_queue_redraw_center();
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)g_malloc(sizeof(dt_lib_snapshots_t));
  self->data = (void *)d;
  memset(d,0,sizeof(dt_lib_snapshots_t));

  /* initialize snapshot storages */
  d->size = 4;
  d->num_snapshots = 0;
  //d->snapshot = (dt_lib_snapshot_t *)g_malloc(sizeof(dt_lib_snapshot_t)*d->size);
  d->vp_xpointer = 0.5;
  d->vp_ypointer = 0.5;
  d->vertical = TRUE;
  //memset(d->snapshot,0,sizeof(dt_lib_snapshot_t)*d->size);

  d->image_backbuf = d->snapshot_backbuf = NULL;

  /* initialize ui containers */
  self->widget = gtk_vbox_new(FALSE,2);
  d->snapshots_box = gtk_vbox_new(FALSE,0);

  /* create take snapshot button */
  GtkWidget *button = gtk_button_new_with_label(_("take snapshot"));
  d->take_button = button;
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(_lib_snapshots_add_button_clicked_callback), self);
  g_object_set(button, "tooltip-text",
               _("take snapshot to compare with another image or the same image at another stage of development"),
               (char *)NULL);

  button = gtk_toggle_button_new_with_label(_("split"));
  d->split_button = button;
  g_signal_connect(G_OBJECT(button), "toggled", G_CALLBACK(_lib_snapshots_split_button_toggled_callback), self);
  g_object_set(button, "tooltip-text",
               _("show split view"),
               (char *)NULL);

  /*
   * initialize snapshots
   */
  //char wdname[32]= {0};
  char localtmpdir[4096]= {0};
  dt_loc_get_tmp_dir (localtmpdir,4096);

  sqlite3_stmt *stmt;
  int imgid = darktable.develop->image_storage.id;

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "SELECT num,name FROM snapshots WHERE imgid = ?1",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    //int num = sqlite3_column_int(stmt, 0);
    d->num_snapshots++;
    gchar *name = (gchar *) sqlite3_column_text(stmt, 1);

    /* create snapshot button */
    dt_lib_snapshot_t *snapshot = (dt_lib_snapshot_t *)g_malloc(sizeof(dt_lib_snapshot_t));
    memset(snapshot,0,sizeof(dt_lib_snapshot_t)); 

    snapshot->button = dtgtk_togglebutton_new_with_label (name,NULL,CPF_STYLE_FLAT);
    
    g_signal_connect(G_OBJECT ( snapshot->button), "clicked",
                     G_CALLBACK (_lib_snapshots_toggled_callback),
                     self);

    /* assign snapshot number to widget */
    g_object_set_data(G_OBJECT(snapshot->button),"snapshot",GINT_TO_POINTER(d->num_snapshots));

    /* setup filename for snapshot */
    /* FIXME: we have to recreate this file at some point */
    snprintf(snapshot->filename, 512, "%s/dt_snapshot_%d.png",localtmpdir,d->num_snapshots);

    /* add button to snapshot box */
    gtk_box_pack_start(GTK_BOX(d->snapshots_box),snapshot->button,TRUE,TRUE,0);

    /* prevent widget to show on external show all */
    //gtk_widget_set_no_show_all(snapshot->button, TRUE);
    d->snapshot = g_list_append(d->snapshot, snapshot);
  }

  /* add snapshot box and take snapshot button to widget ui*/
  gtk_box_pack_start(GTK_BOX(self->widget), d->snapshots_box,TRUE,TRUE,0);

  GtkWidget *hbox = gtk_hbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), d->take_button, TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(hbox), d->split_button, TRUE,TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE,TRUE, 0);

}

void gui_cleanup(dt_lib_module_t *self)
{
  //dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;

  // FIXME: Free the list properly
  //g_free(d->snapshot);

  g_free(self->data);
  self->data = NULL;
}

static void _lib_snapshots_split_button_toggled_callback(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t*)user_data;
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;

  d->split = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  dt_control_queue_redraw_center();
}

static void _lib_snapshots_write_name(const gchar *name, int num, int imgid)
{
  sqlite3_stmt *stmt;

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "insert into snapshots (imgid, num, name) values (?1, ?2, ?3)",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, num);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, name, -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);

  sqlite3_finalize(stmt);
}

static void _lib_snapshots_add_button_clicked_callback(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t*)user_data;
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;

  /* backup last snapshot slot */
  //dt_lib_snapshot_t last = d->snapshot[d->size-1];

  /* rotate slots down to make room for new one on top */
  /*
  for (int k = d->size-1; k > 0; k--)
  {
    GtkWidget *b = d->snapshot[k].button;
    d->snapshot[k] = d->snapshot[k-1];
    d->snapshot[k].button = b;
    gtk_button_set_label(GTK_BUTTON(d->snapshot[k].button),
                         gtk_button_get_label(GTK_BUTTON(d->snapshot[k-1].button)));
  }
  */

  /* update top slot with new snapshot */
  dt_lib_snapshot_t *snapshot = (dt_lib_snapshot_t *)g_malloc(sizeof(dt_lib_snapshot_t));
  memset(snapshot,0,sizeof(dt_lib_snapshot_t));

  char label[64];
  //GtkWidget *b = snapshot->button;
  //d->snapshot[0] = last;
  //d->snapshot[0].button = b;
  const gchar *name = _("original");
  if (darktable.develop->history_end > 0)
  {
    dt_iop_module_t *module  = ((dt_dev_history_item_t *)g_list_nth_data(darktable.develop->history,
                                darktable.develop->history_end-1))->module;
    if (module)
      name = module->name();
    else
      name = _("unknown");
  }
  g_snprintf(label,64,"%s (%d)", name, darktable.develop->history_end);
  snapshot->button = gtk_toggle_button_new_with_label(label);
  g_signal_connect(G_OBJECT(snapshot->button), "toggled", G_CALLBACK(_lib_snapshots_toggled_callback), self);
  
  /* update the snapshots count */
  d->num_snapshots++;
    
  /* assign snapshot number to widget */
  g_object_set_data(G_OBJECT(snapshot->button),"snapshot",GINT_TO_POINTER(d->num_snapshots));

  //dt_lib_snapshot_t *s = d->snapshot + 0;
  DT_CTL_GET_GLOBAL (snapshot->zoom_y, dev_zoom_y);
  DT_CTL_GET_GLOBAL (snapshot->zoom_x, dev_zoom_x);
  DT_CTL_GET_GLOBAL (snapshot->zoom, dev_zoom);
  DT_CTL_GET_GLOBAL (snapshot->closeup, dev_closeup);
  DT_CTL_GET_GLOBAL (snapshot->zoom_scale, dev_zoom_scale);

  /* show active snapshot slots */
  //for (uint32_t k=0; k < d->num_snapshots; k++)
  //  gtk_widget_show(d->snapshot[k].button);

  /* request a new snapshot for top slot */
  /* dt_dev_snapshot_request(darktable.develop, (const char *)&snapshot->filename); */

  /* write snapshot name and history in the db */
  _lib_snapshots_write_name (name,  d->num_snapshots, darktable.develop->image_storage.id );
  dt_dev_write_snapshot_history(darktable.develop, d->num_snapshots);
  
  /* add button to snapshot box */
  gtk_box_pack_start(GTK_BOX(d->snapshots_box),snapshot->button,TRUE,TRUE,0);
  gtk_widget_show(GTK_WIDGET(snapshot->button));

  d->snapshot = g_list_append(d->snapshot, snapshot);
}

static void
_lib_toggle_snapshots_buttons (gpointer data, gpointer user_data)
{
  dt_lib_snapshot_t *snapshot = (dt_lib_snapshot_t *)data;
  GtkWidget *button = (GtkWidget *)user_data;

  if (GTK_WIDGET(button) != snapshot->button)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(snapshot->button), FALSE);
}

static void _lib_snapshots_toggled_callback(GtkToggleButton *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t*)user_data;
  dt_lib_snapshots_t *d = (dt_lib_snapshots_t *)self->data;
  /* get current snapshot index */
  int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"snapshot"));

  /* free current snapshot image if exists */
  if (d->snapshot_image)
  {
    cairo_surface_destroy(d->snapshot_image);
    d->snapshot_image = NULL;
  }
  if (d->image_backbuf)
  {
    g_free(d->image_backbuf);
    d->image_backbuf = NULL;
  }
  if (d->snapshot_backbuf)
  {
    g_free(d->snapshot_backbuf);
    d->snapshot_backbuf = NULL;
  }

  dt_view_manager_t *vm = darktable.view_manager;

  dt_view_t *v = vm->view + vm->current_view;

  dt_develop_t *dev = darktable.develop;

  /* check if snapshot is activated */
  if (gtk_toggle_button_get_active(widget))
  {
    /* lets inactivate all togglebuttons except for self */
    g_list_foreach (d->snapshot, (GFunc)_lib_toggle_snapshots_buttons, (gpointer) widget); 

    /* setup snapshot */
    d->selected = which;
    dt_lib_snapshot_t *s = g_list_nth_data(d->snapshot, which-1);
    DT_CTL_SET_GLOBAL(dev_zoom_y,     s->zoom_y);
    DT_CTL_SET_GLOBAL(dev_zoom_x,     s->zoom_x);
    DT_CTL_SET_GLOBAL(dev_zoom,       s->zoom);
    DT_CTL_SET_GLOBAL(dev_closeup,    s->closeup);
    DT_CTL_SET_GLOBAL(dev_zoom_scale, s->zoom_scale);

    dt_dev_invalidate(darktable.develop);

    d->snapshot_image = cairo_image_surface_create_from_png(s->filename);

    v->call_expose = FALSE;
  }
  else
  {
    v->call_expose = TRUE;
    d->selected = -1;
    //dt_dev_clear_history_items(dev);
    //dt_dev_read_history(dev);
    
    /* FIXME: leaking here, we should be cleaning the list
    if (dev->history != NULL)
    */
    if (dev->previous_history)
    {
      dev->history = dev->previous_history;
      //FIXME and leaking here as well
      dev->previous_history = NULL;
      dev->history_end = g_list_length (dev->history);
      dt_control_signal_raise(darktable.signals,DT_SIGNAL_DEVELOP_HISTORY_CHANGE);
      // FIXME: we probably don't need to reprocess preview
      dt_dev_invalidate(dev);
      dev->pipe->changed |= DT_DEV_PIPE_REMOVE;
    }
    
    printf ("We should be getting back here the old history\n");
    // FIXME: there is an improvement here... we can inject back the old
    // buffer and set the image as not dirty, so it doesn't need to be computed again
  }
    
    /* redraw center view */
    dt_control_queue_redraw_center();

}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
