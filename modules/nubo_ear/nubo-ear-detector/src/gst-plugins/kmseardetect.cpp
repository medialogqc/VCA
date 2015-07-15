#include "kmseardetect.h"

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

#define PLUGIN_NAME "nuboeardetector"
#define FACE_WIDTH 160
#define EAR_WIDTH 320
#define DEFAULT_FILTER_TYPE (KmsEarDetectType)0

#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_profileface.xml"
#define REAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_leftear.xml"
#define LEAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_rightear.xml"

#define LEFT_SIDE 	0
#define RIGHT_SIDE 	1
#define FACE_STEP 	0
#define EAR_STEP 	1

#define TOP_PERCENTAGE 20
#define DOWN_PERCENTAGE 20

// We are going to discard 1 image every the value of DISCARD_FRAME
#define DISCARD_FRAME 2 
//number of pixels to widen the face area
#define EXTRA_ROI	50
using namespace cv;

#define KMS_EAR_DETECT_LOCK(ear_detect)					\
  (g_rec_mutex_lock (&( (KmsEarDetect *) ear_detect)->priv->mutex))

#define KMS_EAR_DETECT_UNLOCK(ear_detect)				\
  (g_rec_mutex_unlock (&( (KmsEarDetect *) ear_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_ear_detect_debug_category);	
#define GST_CAT_DEFAULT kms_ear_detect_debug_category

#define KMS_EAR_DETECT_GET_PRIVATE(obj) (				\
					 G_TYPE_INSTANCE_GET_PRIVATE (	\
								      (obj), \
								      KMS_TYPE_EAR_DETECT, \
								      KmsEarDetectPrivate \
									) \
					     )
enum {
  PROP_0,
  PROP_VIEW_EARS,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA
};

struct _KmsEarDetectPrivate {

  IplImage *img_orig;
    
  int img_width;
  int img_height;
  float scale_f2o;//origin 2 face
  float scale_f2e;//face 2 ear
  float scale_e2o;//mounth 2 origin 
  int view_ears;
  int show_faces;
  int discard;
  GRecMutex mutex;
  gboolean debug;
  int detect_event;
  int meta_data;
  vector<Rect> *faces;
  vector<Rect> *ears;
  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the ears as metadata 
};

/* pad templates */
#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsEarDetect, kms_ear_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_ear_detect_debug_category,
						  PLUGIN_NAME, 0,
						  
						  "debug category for sample_filter element") );
static CascadeClassifier fcascade;
static CascadeClassifier lecascade;
static CascadeClassifier recascade;

static int
kms_ear_detect_init_cascade()
{
  if (!fcascade.load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!lecascade.load(LEAR_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load ear cascade" << std::endl;
      return -1;
    }

  
  if (!recascade.load(REAR_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load ear cascade" << std::endl;
      return -1;
    }

  
  return 0;
}

static void kms_ear_send_event(KmsEarDetect *ear_detect,GstVideoFrame *frame)
{
  GstStructure *face,*ear;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=ear_detect->priv->faces;
  vector<Rect> *ed=ear_detect->priv->ears;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		
  for(  vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      face = gst_structure_new("face_profile",
			       "type", G_TYPE_STRING,"face_profile", 
			       "x", G_TYPE_UINT,(guint) r->x, 
			       "y", G_TYPE_UINT,(guint) r->y, 
			       "width",G_TYPE_UINT, (guint)r->width,
			       "height",G_TYPE_UINT, (guint)r->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);
    }
  
  for(  vector<Rect>::const_iterator m = ed->begin(); m != ed->end(); m++,i++ )
    {
      ear = gst_structure_new("ear",
			       "type", G_TYPE_STRING,"ear", 
			       "x", G_TYPE_UINT,(guint) m->x, 
			       "y", G_TYPE_UINT,(guint) m->y, 
			       "width",G_TYPE_UINT, (guint)m->width,
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, ear,NULL);
      gst_structure_free(ear);
    }
  /*post a faces detected event to src pad*/
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(ear_detect->base.element.srcpad, event);
	
}

static void
kms_ear_detect_conf_images (KmsEarDetect *ear_detect,
			    GstVideoFrame *frame, GstMapInfo &info)
{

  ear_detect->priv->img_height = frame->info.height;
  ear_detect->priv->img_width  = frame->info.width;

  if ( ((ear_detect->priv->img_orig != NULL)) &&
       ((ear_detect->priv->img_orig->width != frame->info.width)
	|| (ear_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&ear_detect->priv->img_orig);
      ear_detect->priv->img_orig = NULL;
    }

  if (ear_detect->priv->img_orig == NULL)

    ear_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							   frame->info.height),
						    IPL_DEPTH_8U, 3);

  ear_detect->priv->scale_f2o = ((float)frame->info.width) / ((float)FACE_WIDTH);
  ear_detect->priv->scale_e2o = ((float)frame->info.width) / ((float)EAR_WIDTH);
  ear_detect->priv->scale_f2e = ((float)ear_detect->priv->scale_f2o)/((float)ear_detect->priv->scale_e2o);
  
  ear_detect->priv->img_orig->imageData = (char *) info.data;
}

static void
kms_ear_detect_set_property (GObject *object, guint property_id,
			     const GValue *value, GParamSpec *pspec)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (object);

  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_EAR_DETECT_LOCK (ear_detect);

  switch (property_id) {
  
  case  PROP_VIEW_EARS:
    ear_detect->priv->view_ears = g_value_get_int (value);
    break;
       
  case PROP_DETECT_BY_EVENT:
    ear_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    ear_detect->priv->meta_data =  g_value_get_int(value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_EAR_DETECT_UNLOCK (ear_detect);
}

static void
kms_ear_detect_get_property (GObject *object, guint property_id,
			     GValue *value, GParamSpec *pspec)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_EAR_DETECT_LOCK (ear_detect);

  switch (property_id) {

  case PROP_VIEW_EARS:
    g_value_set_int (value, ear_detect->priv->view_ears);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,ear_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,ear_detect->priv->meta_data);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_EAR_DETECT_UNLOCK (ear_detect);
}


static void kms_ear_detect_find_ears(KmsEarDetect *ear_detect,const Mat& face_img,const Mat& ear_img,
				     CascadeClassifier *ear_cascade, double scale_f2e,double scale_e2o,
				     double scale_f2o,int side)
{
  vector<Rect> faces;
  vector<Rect> ears;
  Scalar color;
  Mat earROI;
  int i = 0,j=0;
  vector<Rect> *faces_end=ear_detect->priv->faces;
  vector<Rect> *ears_end=ear_detect->priv->ears;
  
  const static Scalar colors[] =  { CV_RGB(0,0,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,255,255),
				    CV_RGB(0,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(255,255,0),
				    CV_RGB(255,0,0),
				    CV_RGB(255,0,255)};	

  //detecting faces
  fcascade.detectMultiScale( face_img, faces,
			     1.1, 2,
			     0 |CV_HAAR_SCALE_IMAGE,
			     Size(3, 3) );
  
  color = colors[i%8];
   
  for( vector<Rect>::iterator r = faces.begin(); r != faces.end(); r++, i++ )
    {
      const int top_height=cvRound((float)r->height*TOP_PERCENTAGE/100);
      const int down_height=cvRound((float)r->height*DOWN_PERCENTAGE/100);

      //printing faces
      if (1 == ear_detect->priv->show_faces)
	{
	  if (LEFT_SIDE == side)
	    cvRectangle( ear_detect->priv->img_orig, 
			 cvPoint(cvRound((r->x + (r->width/2))*scale_f2o), 
				 cvRound((r->y +top_height )*scale_f2o)),
			 cvPoint(cvRound((r->x + r->width-1)*scale_f2o+ EXTRA_ROI), 
				 cvRound((r->y + r->height-down_height)*scale_f2o)),
			 color, 3, 8, 0);
	  else       
	    cvRectangle( ear_detect->priv->img_orig, 
			 cvPoint(cvRound(((face_img.cols- r->x - r->width ))*scale_f2o-EXTRA_ROI), 
				 cvRound(( r->y+top_height)*scale_f2o)),
			 cvPoint(cvRound((face_img.cols-r->x - (r->width/2))*scale_f2o), 
				 cvRound((r->y + r->height-down_height)*scale_f2o)),
			 color, 3, 8, 0);	

	}
      //Transforming the point detected in face image to ear coordinates
      //we only take the down half of the face to avoid excessive processing	
      if (LEFT_SIDE == side)
	{	 
	  r->y=(r->y+top_height)*scale_f2e;
	  r->x=(r->x + (r->width/2))*scale_f2e;
	  r->height = (r->height-down_height)*scale_f2e;
	  r->width = (r->width/2 )*scale_f2e+EXTRA_ROI;
	  ////EXTRA_ROI As the ear is in the extreme of the face, 
	  //we need an extra area to detect it in right way
	 
	  if (r->x + r->width > ear_img.cols) r->width=ear_img.cols - r->x -1; 
	}
      else 
	{
	  r->y=(r->y + top_height)*scale_f2e;
	  r->x=(face_img.cols- r->x - r->width)*scale_f2e-EXTRA_ROI;
	  r->height = (r->height-down_height)*scale_f2e;
	  r->width = (r->width/2)*scale_f2e;        
	  //EXTRA_ROI: As the ear is in the extreme of the face, 
	  //we need an extra area to detect it in right way
	  if (r->x<0 ) r->x=0;
	}
      
      if (ears.size() > 0)
	ears.clear();
	      	 
      earROI = ear_img(*r);
      ear_cascade->detectMultiScale( earROI, ears,
				     1.1, 3, 
				     0| CV_HAAR_FIND_BIGGEST_OBJECT,
				     Size(1, 1));

      faces_end->push_back(*r);

	for ( vector<Rect>::iterator m = ears.begin(); m != ears.end();m++,j++)
	  {
	    ears_end->push_back(*m);
	    //Transforming the point detected in ear image to oring coordinates
	    if (1 == ear_detect->priv->view_ears)
	      cvRectangle( ear_detect->priv->img_orig, 
			   cvPoint(cvRound((r->x + m->x)*scale_e2o), 
				   cvRound((r->y+m->y)*scale_e2o)),
			   cvPoint(cvRound(((r->x + m->x)+ m->width-1)*scale_e2o), 
				   cvRound(((r->y+m->y) + m->height-1)*scale_e2o)),
			   color, 3, 8, 0);	    
	  }
      
    }
}

  


static void kms_ear_detect_process_frame(KmsEarDetect *ear_detect,int width,int height,
					 double scale_f2e,double scale_e2o, double scale_f2o)
{  
    
  Mat img (ear_detect->priv->img_orig);
  Mat ear_frame (cvRound(img.rows/scale_e2o), cvRound(img.cols/scale_e2o), CV_8UC1);
  Mat gray;
  Mat  leftImg( cvRound (img.rows/scale_f2o), cvRound(img.cols/scale_f2o), CV_8UC1 );
  Mat  rightImg( cvRound (img.rows/scale_f2o), cvRound(img.cols/scale_f2o), CV_8UC1 );
  

  cvtColor( img, gray, CV_BGR2GRAY );
  //setting up the image where the face detector will be executed
  resize( gray, leftImg, leftImg.size(), 0, 0, INTER_LINEAR );
  equalizeHist( leftImg, leftImg );
  flip(leftImg,rightImg,1);//flip around y-axis
    
  //setting up the image where the ear detector will be executed
  resize(gray,ear_frame,ear_frame.size(), 0,0,INTER_LINEAR);
  equalizeHist( ear_frame, ear_frame);
      
  kms_ear_detect_find_ears(ear_detect,leftImg,ear_frame,&lecascade,scale_f2e,scale_e2o,scale_f2o,LEFT_SIDE);
  kms_ear_detect_find_ears(ear_detect,rightImg,ear_frame,&recascade,scale_f2e,scale_e2o,scale_f2o,RIGHT_SIDE);
      
}


 

/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_ear_detect_transform_frame_ip (GstVideoFilter *filter,
				   GstVideoFrame *frame)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (filter);
  GstMapInfo info;
  double scale_f2o=0.0,scale_e2o=0.0,scale_f2e=0.0;
  int width=0,height=0;
  struct timeval start_t,end_t;

  if (ear_detect->priv->discard % DISCARD_FRAME == DISCARD_FRAME -1 )
    {
      ear_detect->priv->discard=0;
      return GST_FLOW_OK;
    }
  ear_detect->priv->discard+=1;

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  // setting up images
  kms_ear_detect_conf_images (ear_detect, frame, info);

  KMS_EAR_DETECT_LOCK (ear_detect);

  scale_f2e = ear_detect->priv->scale_f2e;
  scale_e2o= ear_detect->priv->scale_e2o;
  scale_f2o = ear_detect->priv->scale_f2o;
  width = ear_detect->priv->img_width;
  height = ear_detect->priv->img_height;

  KMS_EAR_DETECT_UNLOCK (ear_detect);

  gettimeofday(&start_t,NULL);	
  kms_ear_detect_process_frame(ear_detect,width,height,scale_f2e,scale_e2o,scale_f2o);
  gettimeofday(&end_t,NULL);
  
  if (1==ear_detect->priv->meta_data)
    kms_ear_send_event(ear_detect,frame);

  ear_detect->priv->faces->clear();
  ear_detect->priv->ears->clear();

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
kms_ear_detect_dispose (GObject *object)
{
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_ear_detect_finalize (GObject *object)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT(object);

  cvReleaseImage (&ear_detect->priv->img_orig);
  delete ear_detect->priv->faces;
  delete ear_detect->priv->ears;
  g_rec_mutex_clear(&ear_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_ear_detect_init (KmsEarDetect *
		     ear_detect)
{
  int ret=0;
  ear_detect->priv = KMS_EAR_DETECT_GET_PRIVATE (ear_detect);
  ear_detect->priv->scale_f2e=1.0;
  ear_detect->priv->scale_e2o=1.0;
  ear_detect->priv->scale_f2o=1.0;
  ear_detect->priv->img_width = 320;
  ear_detect->priv->img_height = 240;
  ear_detect->priv->img_orig = NULL;
  ear_detect->priv->view_ears=-1;
  ear_detect->priv->show_faces=0;
  ear_detect->priv->discard=0;
  ear_detect->priv->faces= new vector<Rect>;
  ear_detect->priv->ears= new vector<Rect>;

  if (fcascade.empty())
    if (kms_ear_detect_init_cascade() < 0)      
      GST_DEBUG("Error: init cascade");
  
  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");
  
  g_rec_mutex_init(&ear_detect->priv->mutex);
  
}

static void
kms_ear_detect_class_init (KmsEarDetectClass *ear)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (ear);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (ear);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (ear),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (ear),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (ear),
					 "ear detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_ear_detect_set_property;
  gobject_class->get_property = kms_ear_detect_get_property;
  gobject_class->dispose = kms_ear_detect_dispose;
  gobject_class->finalize = kms_ear_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_EARS,
				   g_param_spec_int ("view-ears", "view ears",
						     "To indicate whether or not we have to draw  the detected ears on the stream",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );


  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event. This algorithm does not support any specific event at the moment. Therefore this property should not be used",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the ear and profile face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_ear_detect_transform_frame_ip);

  /*Properties initialization*/
  g_type_class_add_private (ear, sizeof (KmsEarDetectPrivate) );
}

gboolean
kms_ear_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_EAR_DETECT);
}
