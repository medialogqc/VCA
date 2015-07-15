#include "kmsfacedetect.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>
#include <string>
#include <unistd.h>
#include <iostream>
#include <opencv2/opencv.hpp>


#define PLUGIN_NAME "nubofacedetector"
#define MAX_WIDTH 160
#define DEFAULT_FILTER_TYPE (KmsFaceDetectType)0
#define NUM_FRAMES_TO_PROCESS 10

#define HAAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"

using namespace cv;

#define KMS_FACE_DETECT_LOCK(face_detect)				\
  (g_rec_mutex_lock (&( (KmsFaceDetect *) face_detect)->priv->mutex))

#define KMS_FACE_DETECT_UNLOCK(face_detect)				\
  (g_rec_mutex_unlock (&( (KmsFaceDetect *) face_detect)->priv->mutex))

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define MOTION_EVENT "motion"

GST_DEBUG_CATEGORY_STATIC (kms_face_detect_debug_category);
#define GST_CAT_DEFAULT kms_face_detect_debug_category

#define KMS_FACE_DETECT_GET_PRIVATE(obj) (				\
					  G_TYPE_INSTANCE_GET_PRIVATE (	\
								       (obj), \
								       KMS_TYPE_FACE_DETECT, \
								       KmsFaceDetectPrivate \
									) \
									)
enum {
  PROP_0,
  PROP_VIEW_FACES,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,
  PROP_SHOW_DEBUG_INFO
};


struct _KmsFaceDetectPrivate {

  IplImage *img_orig;
  IplImage *img_resized;
  CvMemStorage *cv_mem_storage;
  CvSeq *face_seq;
  vector<Rect> *faces_detected;
  int img_width;
  int img_height;
  float scale;
  int show_faces; // to draw a rectangle over the face
  int detect_event;  
  int meta_data;
  int num_frames_to_process;
  CascadeClassifier *cascade;
  GstClockTime dts,pts;
  GQueue *events_queue;
  GRecMutex mutex;
  gboolean debug;

  /*detect_event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the face as metadata 
  /*num_frames_to_process*/
  // When we receive an event we need to process at least NUM_FRAMES_TO_PROCESS

};
/* pad templates */
#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */
G_DEFINE_TYPE_WITH_CODE (KmsFaceDetect, kms_face_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_face_detect_debug_category,
						  PLUGIN_NAME, 0,

						  "debug category for sample_filter element") );
static CascadeClassifier cascade;

static int
kms_face_detect_init_cascade()
{
  if (!cascade.load(HAAR_CONF_FILE))
    {
      fprintf(stderr,"Error charging cascade");
      return -1;
    }

  if (cascade.empty())
    fprintf(stderr,"****CASCADE IS EMPTY***********");

  return 0;
}

static void kms_face_send_event(KmsFaceDetect *face_detect,GstVideoFrame *frame)
{
  GstStructure *face;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char face_id[10];
  vector<Rect> *fd=face_detect->priv->faces_detected;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		
  for( vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      face = gst_structure_new("face",
			       "type", G_TYPE_STRING,"face", 
			       "x", G_TYPE_UINT,(guint) r->x, 
			       "y", G_TYPE_UINT,(guint) r->y, 
			       "width",G_TYPE_UINT, (guint)r->width,
			       "height",G_TYPE_UINT, (guint)r->height,
			       NULL);
      sprintf(face_id,"%d",i);
      gst_structure_set(message,face_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);
    }
	
  /*post a faces detected event to src pad*/
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(face_detect->base.element.srcpad, event);

}

static gboolean kms_face_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsFaceDetect *face = KMS_FACE_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (face);

      message = gst_structure_copy (gst_event_get_structure (event));
      g_queue_push_tail (face->priv->events_queue, message);

      GST_OBJECT_UNLOCK (face);
      break;
    }
  default:
    break;
  }
  ret = gst_pad_push_event (trans->srcpad, event);


  return ret;
}


static void
kms_face_detect_conf_images (KmsFaceDetect *face_detect,
                             GstVideoFrame *frame, GstMapInfo &info)
{

  face_detect->priv->img_height = frame->info.height;
  face_detect->priv->img_width  = frame->info.width;

  if ( ((face_detect->priv->img_orig != NULL)) &&
       ((face_detect->priv->img_orig->width != frame->info.width)
	|| (face_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&face_detect->priv->img_orig);
      face_detect->priv->img_orig = NULL;
    }

  if (face_detect->priv->img_orig == NULL)

    face_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							    frame->info.height),
						     IPL_DEPTH_8U, 3);
  if ( frame->info.width > MAX_WIDTH )
    face_detect->priv->scale = frame->info.width / MAX_WIDTH;
  else
    face_detect->priv->scale=1;

  face_detect->priv->img_orig->imageData = (char *) info.data;
}


static void
kms_face_detect_set_property (GObject *object, guint property_id,
			      const GValue *value, GParamSpec *pspec)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (object);
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_FACE_DETECT_LOCK (face_detect);

  switch (property_id) {
  case PROP_VIEW_FACES:
    face_detect->priv->show_faces =  g_value_get_int (value);
    break;

  case PROP_DETECT_BY_EVENT:
    face_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    face_detect->priv->meta_data =  g_value_get_int(value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_FACE_DETECT_UNLOCK (face_detect);
  
}

static void
kms_face_detect_get_property (GObject *object, guint property_id,

			      GValue *value, GParamSpec *pspec)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_FACE_DETECT_LOCK (face_detect);

  switch (property_id) {

  case PROP_VIEW_FACES:
    g_value_set_int (value, face_detect->priv->show_faces);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,face_detect->priv->detect_event);
    break;

  case PROP_SEND_META_DATA:
    g_value_set_int(value,face_detect->priv->meta_data);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_FACE_DETECT_UNLOCK (face_detect);
}

static gboolean __get_timestamp(KmsFaceDetect *face,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {

    gst_structure_get(ts,"dts",G_TYPE_UINT64,
		      &face->priv->dts,NULL);

    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &face->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}


static bool __get_message(GstStructure *message)
{
  gint len,aux;
  bool result=false;
  //int movement=-1;
  gchar *grid=0;
  len = gst_structure_n_fields (message);

  for (aux = 0; aux < len; aux++) {
    GstStructure *data;
    gboolean ret;

    const gchar *name = gst_structure_nth_field_name (message, aux);
    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }

    if (0 == g_strcmp0 (name, MOTION_EVENT))
      {
	ret = gst_structure_get (message, name, GST_TYPE_STRUCTURE, &data, NULL);

	if (ret) {
	    				
	  gst_structure_get (data, "grid", G_TYPE_STRING, &grid, NULL);
	  gst_structure_free (data);
	  result=true;
	}
      }
  }

  return result;
}


static bool __process_alg(KmsFaceDetect *face_detect, GstClockTime f_pts)
{
  GstStructure *message;
  bool res=false;
  gboolean ret = false;
  //if detect_event is false it does not matter the event received
  if (0==face_detect->priv->detect_event) return true;

  if (g_queue_get_length(face_detect->priv->events_queue) == 0) 
    return false;
	
  message= (GstStructure *) g_queue_pop_head(face_detect->priv->events_queue);

  if (NULL != message)
    {
      ret=__get_timestamp(face_detect,message);
	
      //if ( ret && face_detect->priv->pts == f_pts)
      if ( ret )
	{

	  res = __get_message(message);	  
	}

    }

  if (res) 
    face_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS;
  
  return res;
}


static void
kms_face_detect_process_frame(KmsFaceDetect *face_detect,int width,int height,double scale,
			      GstClockTime pts)
{
  Mat img (face_detect->priv->img_orig);
  Scalar color;
  Mat aux_img (cvRound (img.rows/scale), cvRound(img.cols/scale), CV_8UC3 );
  Mat img_gray(cvRound (img.rows/scale), cvRound(img.cols/scale), CV_8UC1 );
  vector<Rect> *faces = face_detect->priv->faces_detected;

  int i=0;
  const static Scalar colors[] =  { CV_RGB(0,0,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,255,255),
				    CV_RGB(0,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(255,255,0),
				    CV_RGB(255,0,0),
				    CV_RGB(255,0,255)} ;

  //check if the process of the algorithm is true or if we receive and event.
  if ( ! __process_alg(face_detect,pts) && face_detect->priv->num_frames_to_process <= 0 )
    return;

  face_detect->priv->num_frames_to_process --;

  cv::resize( img,aux_img,  aux_img.size(), 0, 0, INTER_LINEAR );
  cvtColor( aux_img, img_gray, CV_BGR2GRAY );
  equalizeHist( img_gray, img_gray );


  faces->clear();
  cascade.detectMultiScale(img_gray,*faces,1.2,3,0,
			   Size(img_gray.cols/20,img_gray.rows/20 ),
			   Size(img_gray.cols/2,img_gray.rows/2 ));

	
  for( vector<Rect>::const_iterator r = faces->begin(); r != faces->end(); r++,i++ )
    {

      color = colors[i%8];
      if (face_detect->priv->show_faces > 0)
	cvRectangle( face_detect->priv->img_orig, cvPoint(cvRound(r->x*scale), 
							  cvRound(r->y*scale)),
		     cvPoint(cvRound((r->x + r->width-1)*scale), 
			     cvRound((r->y + r->height-1)*scale)),
		     color, 3, 8, 0);
    }

}

/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_face_detect_transform_frame_ip (GstVideoFilter *filter,
				    GstVideoFrame *frame)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (filter);
  GstMapInfo info;
  double scale=0.0;
  int width=0,height=0;

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
	
  // setting up images
  kms_face_detect_conf_images (face_detect, frame, info);

  KMS_FACE_DETECT_LOCK (face_detect);

  scale = face_detect->priv->scale;
  width = face_detect->priv->img_width;
  height = face_detect->priv->img_height;

  KMS_FACE_DETECT_UNLOCK (face_detect);
	  
  kms_face_detect_process_frame(face_detect,width,height,scale,frame->buffer->pts);
  if (1 == face_detect->priv->meta_data)
    kms_face_send_event(face_detect,frame);
		
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
 * NULL; g_clear_object() does this for us, atomically.
 */
static void
kms_face_detect_dispose (GObject *object)
{
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_face_detect_finalize (GObject *object)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT(object);


  cvReleaseImage (&face_detect->priv->img_orig);

  if (face_detect->priv->cv_mem_storage != NULL)
    cvClearMemStorage (face_detect->priv->cv_mem_storage);
  if (face_detect->priv->face_seq != NULL)
    cvClearSeq (face_detect->priv->face_seq);

  cvReleaseMemStorage (&face_detect->priv->cv_mem_storage);

  delete face_detect->priv->faces_detected;
  //g_mutex_clear(&face_detect->priv->mutex);
  g_rec_mutex_clear(&face_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_face_detect_init (KmsFaceDetect *
		      face_detect)
{
  int ret=0;
  face_detect->priv = KMS_FACE_DETECT_GET_PRIVATE (face_detect);
  face_detect->priv->scale=1.0;
  face_detect->priv->img_width = DEFAULT_WIDTH;
  face_detect->priv->img_height = DEFAULT_HEIGHT;
  face_detect->priv->img_orig = NULL;
  face_detect->priv->events_queue= g_queue_new();
  face_detect->priv->detect_event=0;
  face_detect->priv->meta_data=0;
  face_detect->priv->faces_detected= new vector<Rect>;
  face_detect->priv->num_frames_to_process=0;

  face_detect->priv->cv_mem_storage=cvCreateMemStorage(0);
  face_detect->priv->face_seq =cvCreateSeq (0, sizeof (CvSeq), sizeof (CvRect),
					    face_detect->priv->cv_mem_storage);
  face_detect->priv->show_faces = 0;

  if (cascade.empty())
    if (kms_face_detect_init_cascade() < 0)
      std::cout << "Error: init cascade" << std::endl;

  face_detect->priv->cascade = & cascade;


  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&face_detect->priv->mutex);
	
}

static void
kms_face_detect_class_init (KmsFaceDetectClass *face)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (face);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (face);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (face),
				     gst_pad_template_new ("src", GST_PAD_SRC,
							   GST_PAD_ALWAYS,
							   gst_caps_from_string (VIDEO_SRC_CAPS)));
  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (face),
				     gst_pad_template_new("sink", GST_PAD_SINK,
							  GST_PAD_ALWAYS,
							  gst_caps_from_string (VIDEO_SINK_CAPS)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (face),
					 "face detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_face_detect_set_property;
  gobject_class->get_property = kms_face_detect_get_property;
  gobject_class->dispose = kms_face_detect_dispose;
  gobject_class->finalize = kms_face_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_FACES,
				   g_param_spec_int ("view-faces", "view faces",
						     "To determine if we have to draw or hide the detected faces on the stream",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );

  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like motion detection",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("send-meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the face as metadata", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_face_detect_transform_frame_ip);

  /*Properties initialization*/
  g_type_class_add_private (face, sizeof (KmsFaceDetectPrivate) );

  face->base_face_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_face_detect_sink_events);


}

gboolean
kms_face_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_FACE_DETECT);
}
