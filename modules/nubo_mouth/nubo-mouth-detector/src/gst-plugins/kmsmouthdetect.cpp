/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Kurento (http://kurento.org/)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * cop ies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Example pipelines. The command has to be launched from the source directory.
 * (with a webcam)
 * gst-launch-1.0 --gst-plugin-path=. v4l2src ! videoconvert !
 *                              samplefilter type=1 ! videoconvert ! autovideosink
 * (with a video)
 * gst-launch-1.0 --gst-plugin-path=. uridecodebin uri=<video_uri> ! videoconvert !
 *                              samplefilter type=0 edge-value=125 ! videoconvert !
 *                              autovideosink
 */
#include "kmsmouthdetect.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>
#include <opencv2/opencv.hpp>


#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#define PLUGIN_NAME "nubomouthdetector"
#define FACE_WIDTH 160
#define MOUTH_WIDTH 640
#define DEFAULT_FILTER_TYPE (KmsMouthDetectType)0
#define NUM_FRAMES_TO_PROCESS 10
#define FACE_TYPE "face"

#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"
#define MOUTH_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_mouth.xml"


using namespace cv;

#define KMS_MOUTH_DETECT_LOCK(mouth_detect)				\
  (g_rec_mutex_lock (&( (KmsMouthDetect *) mouth_detect)->priv->mutex))

#define KMS_MOUTH_DETECT_UNLOCK(mouth_detect)				\
  (g_rec_mutex_unlock (&( (KmsMouthDetect *) mouth_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_mouth_detect_debug_category);
#define GST_CAT_DEFAULT kms_mouth_detect_debug_category

#define KMS_MOUTH_DETECT_GET_PRIVATE(obj) (				\
					   G_TYPE_INSTANCE_GET_PRIVATE ( \
									(obj), \
									KMS_TYPE_MOUTH_DETECT, \
									KmsMouthDetectPrivate \
										) \
					       )
enum {
  PROP_0,
  PROP_VIEW_MOUTHS,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA
};

struct _KmsMouthDetectPrivate {

  IplImage *img_orig;
  int img_width;
  int img_height;
  int view_mouths;
  float scale_o2f;//origin 2 face
  float scale_f2m;//face 2 mouth
  float scale_m2o;//mounth 2 origin 
  GRecMutex mutex;
  gboolean debug;
  GQueue *events_queue;
  GstClockTime pts;
  int num_frames_to_process;
  int detect_event;
  int meta_data;
  vector<Rect> *faces;
  vector<Rect> *mouths;
  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the mouth as metadata 
  /*num_frames_to_process*/
  // When we receive an event we need to process at least NUM_FRAMES_TO_PROCESS
};

/* pad templates */

#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsMouthDetect, kms_mouth_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_mouth_detect_debug_category,
						  PLUGIN_NAME, 0,
						  
						  "debug category for sample_filter element") );
static CascadeClassifier fcascade;
static CascadeClassifier mcascade;

static int
kms_mouth_detect_init_cascade()
{
  if (!fcascade.load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!mcascade.load(MOUTH_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load mouth cascade" << std::endl;
      return -1;
    }
  
  
  return 0;
}


static gboolean kms_mouth_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsMouthDetect *mouth = KMS_MOUTH_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (mouth);

      message = gst_structure_copy (gst_event_get_structure (event));

      g_queue_push_tail (mouth->priv->events_queue, message);

      GST_OBJECT_UNLOCK (mouth);
      break;
    }
  default:
    break;
  }
  ret = gst_pad_push_event (trans->srcpad, event);


  return ret;
}


static void kms_mouth_send_event(KmsMouthDetect *mouth_detect,GstVideoFrame *frame)
{
  GstStructure *face,*mouth;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=mouth_detect->priv->faces;
  vector<Rect> *md=mouth_detect->priv->mouths;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		
  for(  vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      face = gst_structure_new("face",
			       "type", G_TYPE_STRING,"face", 
			       "x", G_TYPE_UINT,(guint) r->x, 
			       "y", G_TYPE_UINT,(guint) r->y, 
			       "width",G_TYPE_UINT, (guint)r->width,
			       "height",G_TYPE_UINT, (guint)r->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);
    }
  
  for(  vector<Rect>::const_iterator m = md->begin(); m != md->end(); m++,i++ )
    {
      mouth = gst_structure_new("mouth",
			       "type", G_TYPE_STRING,"mouth", 
			       "x", G_TYPE_UINT,(guint) m->x, 
			       "y", G_TYPE_UINT,(guint) m->y, 
			       "width",G_TYPE_UINT, (guint)m->width,
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, mouth,NULL);
      gst_structure_free(mouth);
    }
  /*post a faces detected event to src pad*/
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(mouth_detect->base.element.srcpad, event);
	
}

static void
kms_mouth_detect_conf_images (KmsMouthDetect *mouth_detect,
			      GstVideoFrame *frame, GstMapInfo &info)
{

  mouth_detect->priv->img_height = frame->info.height;
  mouth_detect->priv->img_width  = frame->info.width;

  if ( ((mouth_detect->priv->img_orig != NULL)) &&
       ((mouth_detect->priv->img_orig->width != frame->info.width)
	|| (mouth_detect->priv->img_orig->height != frame->info.height)))
    {
      cvReleaseImage(&mouth_detect->priv->img_orig);
      mouth_detect->priv->img_orig = NULL;
    }

  if (mouth_detect->priv->img_orig == NULL)
    mouth_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							     frame->info.height),
						      IPL_DEPTH_8U, 3);

  mouth_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)FACE_WIDTH);
  mouth_detect->priv->scale_m2o= ((float) frame->info.width) / ((float)MOUTH_WIDTH);
  mouth_detect->priv->scale_f2m = ((float)mouth_detect->priv->scale_o2f) / ((float)mouth_detect->priv->scale_m2o);

  mouth_detect->priv->img_orig->imageData = (char *) info.data;

}

static void
kms_mouth_detect_set_property (GObject *object, guint property_id,
			       const GValue *value, GParamSpec *pspec)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (object);
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_MOUTH_DETECT_LOCK (mouth_detect);

  switch (property_id) {
  
  case PROP_VIEW_MOUTHS:
    mouth_detect->priv->view_mouths = g_value_get_int (value);
    break;
    
  case PROP_DETECT_BY_EVENT:
    mouth_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    mouth_detect->priv->meta_data =  g_value_get_int(value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);
}

static void
kms_mouth_detect_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_MOUTH_DETECT_LOCK (mouth_detect);

  switch (property_id) {

  case PROP_VIEW_MOUTHS:
    g_value_set_int (value, mouth_detect->priv->view_mouths);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,mouth_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,mouth_detect->priv->meta_data);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);
}

static gboolean __get_timestamp(KmsMouthDetect *mouth,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {
    
    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &mouth->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}

static bool __get_message(KmsMouthDetect *mouth,
			  GstStructure *message)
{
  gint len,aux;
  bool result=false;
  //int movement=-1;
  int  id=0;
  char struct_id[10];
  const gchar *str_type;

  len = gst_structure_n_fields (message);

  for (aux = 0; aux < len; aux++) {
    GstStructure *data;
    gboolean ret;

    const gchar *name = gst_structure_nth_field_name (message, aux);
    
    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }

    sprintf(struct_id,"%d",id);    
    if ((g_strcmp0(name,struct_id) == 0)) //get structure's id
      {
	id++;
	//getting the structure
	ret = gst_structure_get (message, name, GST_TYPE_STRUCTURE, &data, NULL);
	if (ret) {
	  //type of the structure
	  gst_structure_get (data, "type", G_TYPE_STRING, &str_type, NULL);
	  if (g_strcmp0(str_type,FACE_TYPE)==0)
	    {
	      Rect r;
	      gst_structure_get (data, "x", G_TYPE_UINT, & r.x, NULL);
	      gst_structure_get (data, "y", G_TYPE_UINT, & r.y, NULL);
	      gst_structure_get (data, "width", G_TYPE_UINT, & r.width, NULL);
	      gst_structure_get (data, "height", G_TYPE_UINT, & r.height, NULL);	  	 
	      gst_structure_free (data);
	      mouth->priv->faces->push_back(r);
	    }	  
	  result=true;
	}
      }
  }            
  return result;
}

static bool __process_alg(KmsMouthDetect *mouth_detect, GstClockTime f_pts)
{
  GstStructure *message;
  bool res=false;
  gboolean ret = false;
  //if detect_event is false it does not matter the event received

  if (0==mouth_detect->priv->detect_event) return true;

  if (g_queue_get_length(mouth_detect->priv->events_queue) == 0) 
    return false;
	
  message= (GstStructure *) g_queue_pop_head(mouth_detect->priv->events_queue);

  if (NULL != message)
    {

      ret=__get_timestamp(mouth_detect,message);
       //if ( ret && mouth_detect->priv->pts == f_pts)
      if ( ret )
	{
	  res = __get_message(mouth_detect,message);	  
	}
    }


  if (res) 
    mouth_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS;
  
  return res;
}


static void
kms_mouth_detect_process_frame(KmsMouthDetect *mouth_detect,int width,int height,
			       double scale_f2m,double scale_m2o, double scale_o2f,GstClockTime pts)
{
  int i = 0,j=0;
  Scalar color;
  Mat img (mouth_detect->priv->img_orig);
  Mat gray, mouth_frame (cvRound(img.rows/scale_m2o), cvRound(img.cols/scale_m2o), CV_8UC1);
  Mat  smallImg( cvRound (img.rows/scale_o2f), cvRound(img.cols/scale_o2f), CV_8UC1 );
  Mat mouthROI;
  vector<Rect> *faces=mouth_detect->priv->faces;
  vector<Rect> *mouths =mouth_detect->priv->mouths;
  vector<Rect> mouth;
  Rect r_aux;
  const static Scalar colors[] =  { CV_RGB(255,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(255,0,0),
				    CV_RGB(255,0,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,0,255),
				    CV_RGB(0,255,255),
				    CV_RGB(0,255,0)};	

  faces->clear();
  mouths->clear();


  if ( ! __process_alg(mouth_detect,pts) && mouth_detect->priv->num_frames_to_process <=0)
    return;

  mouth_detect->priv->num_frames_to_process --;

  cvtColor( img, gray, CV_BGR2GRAY );

  //if detect_event != 0 we have received faces as meta-data
  if (0 == mouth_detect->priv->detect_event )
    {
      //setting up the image where the face detector will be executed
      resize( gray, smallImg, smallImg.size(), 0, 0, INTER_LINEAR );
      equalizeHist( smallImg, smallImg );
      //detecting faces
      fcascade.detectMultiScale( smallImg, *faces,
				 1.1, 2, 0
				 |CV_HAAR_SCALE_IMAGE,
				 Size(3, 3) );
    }

    
  //setting up the image where the mouth detector will be executed	
  resize(gray,mouth_frame,mouth_frame.size(), 0,0,INTER_LINEAR);
  equalizeHist( mouth_frame, mouth_frame);
    
    
  for( vector<Rect>::iterator r = faces->begin(); r != faces->end(); r++, i++ )
    {	

      color = colors[i%8];
      if (mouth.size() > 0)
	mouth.clear();
	  
      const int half_height=cvRound((float)r->height/1.8);
      //Transforming the point detected in face image to mouht coordinates
      //we only take the down half of the face to avoid excessive processing
	
      r_aux.y=(r->y + half_height)*scale_f2m;
      r_aux.x=r->x*scale_f2m;
      r_aux.height = half_height*scale_f2m;
      r_aux.width = r->width*scale_f2m;
      
      mouthROI = mouth_frame(r_aux);
      mcascade.detectMultiScale( mouthROI, mouth,
				 1.1, 3, 0
				 |CV_HAAR_FIND_BIGGEST_OBJECT,
				 Size(1, 1));

	for ( vector<Rect>::iterator m = mouth.begin(); m != mouth.end();m++,j++)	  
	  {

	    Rect m_aux;
	    //Transforming the point detected in mouth image to oring coordinates
	    m_aux.x = cvRound((r_aux.x + m->x)*scale_m2o);
	    m_aux.y= cvRound((r_aux.y+m->y)*scale_m2o);
	    m_aux.width=(m->width-1)*scale_m2o;
	    m_aux.height=(m->height-1)*scale_m2o;
	    mouths->push_back(m_aux);

	    if (1 == mouth_detect->priv->view_mouths)
	      
	      cvRectangle( mouth_detect->priv->img_orig, cvPoint(m_aux.x,m_aux.y),
			   cvPoint(cvRound(((r_aux.x + m->x)+ m->width-1)*scale_m2o), 
				   cvRound(((r_aux.y+m->y) + m->height-1)*scale_m2o)),
			   color, 3, 8, 0);	    
	  }
	  
    }
}

/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_mouth_detect_transform_frame_ip (GstVideoFilter *filter,
				     GstVideoFrame *frame)
{
      
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (filter);
  GstMapInfo info;
  double scale_o2f=0.0,scale_m2o=0.0,scale_f2m=0.0;
  int width=0,height=0;
      
  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  // setting up images
  kms_mouth_detect_conf_images (mouth_detect, frame, info);
      
  KMS_MOUTH_DETECT_LOCK (mouth_detect);
      
  scale_f2m = mouth_detect->priv->scale_f2m;
  scale_m2o= mouth_detect->priv->scale_m2o;
  scale_o2f = mouth_detect->priv->scale_o2f;
  width = mouth_detect->priv->img_width;
  height = mouth_detect->priv->img_height;
      
  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);
      
  kms_mouth_detect_process_frame(mouth_detect,width,height,scale_f2m,
				 scale_m2o,scale_o2f,frame->buffer->pts);
	
  if (1==mouth_detect->priv->meta_data)
    kms_mouth_send_event(mouth_detect,frame);
   
  mouth_detect->priv->faces->clear();
  mouth_detect->priv->mouths->clear();
    
  gst_buffer_unmap (frame->buffer, &info);

  return GST_FLOW_OK;
}

/*
 * In dispose(), you are supposed to free all types referenced from this
 * object which might themselves hold a reference to self. Generally,
 * the most simple solution is to unref all members on which you own a
 * reference.
 * dispose() might be called multiple times, so we must guard against
 * calling g_object_unref() on an invalid GObject by setting the member
 * NULL; g_clmouth_object() does this for us, atomically.
 */
static void
kms_mouth_detect_dispose (GObject *object)
{
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_mouth_detect_finalize (GObject *object)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT(object);

  cvReleaseImage (&mouth_detect->priv->img_orig);
  delete mouth_detect->priv->faces;
  delete mouth_detect->priv->mouths;
  g_rec_mutex_clear(&mouth_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_mouth_detect_init (KmsMouthDetect *
		       mouth_detect)
{
  std::cout << "En mouth detect init" << std::endl;
  int ret=0;
  mouth_detect->priv = KMS_MOUTH_DETECT_GET_PRIVATE (mouth_detect);
  mouth_detect->priv->scale_f2m=1.0;
  mouth_detect->priv->scale_m2o=1.0;
  mouth_detect->priv->scale_o2f=1.0;
  mouth_detect->priv->img_width = 320;
  mouth_detect->priv->img_height = 240;
  mouth_detect->priv->img_orig = NULL;
  mouth_detect->priv->view_mouths=0;
  mouth_detect->priv->events_queue= g_queue_new();
  mouth_detect->priv->detect_event=0;
  mouth_detect->priv->meta_data=0;
  mouth_detect->priv->faces= new vector<Rect>;
  mouth_detect->priv->mouths= new vector<Rect>;
  mouth_detect->priv->num_frames_to_process=0;

  if (fcascade.empty())
    if (kms_mouth_detect_init_cascade() < 0)
      std::cout << "Error: init cascade" << std::endl;

  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&mouth_detect->priv->mutex);

  std::cout << "En mouth detect init END" << std::endl;
}

static void
kms_mouth_detect_class_init (KmsMouthDetectClass *mouth)
{
    
  GObjectClass *gobject_class = G_OBJECT_CLASS (mouth);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (mouth);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (mouth),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (mouth),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (mouth),
					 "mouth detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_mouth_detect_set_property;
  gobject_class->get_property = kms_mouth_detect_get_property;
  gobject_class->dispose = kms_mouth_detect_dispose;
  gobject_class->finalize = kms_mouth_detect_finalize;

  //properties definition


  g_object_class_install_property (gobject_class, PROP_VIEW_MOUTHS,
				   g_param_spec_int ("view-mouths", "view mouths",
						     "To indicate whether or not we have to draw  the detected mouths on the stream ",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );


  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like face detection",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the mouth and face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));


  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_mouth_detect_transform_frame_ip);

  /*Properties initialization*/
  g_type_class_add_private (mouth, sizeof (KmsMouthDetectPrivate) );

  mouth->base_mouth_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_mouth_detect_sink_events);

}

gboolean
kms_mouth_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_MOUTH_DETECT);
}
