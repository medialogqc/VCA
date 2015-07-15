/* Autogenerated with kurento-module-creator */

#include <gst/gst.h>
#include "MediaPipeline.hpp"
#include "MediaPipelineImpl.hpp"
#include <NuboFaceDetectorImplFactory.hpp>
#include "NuboFaceDetectorImpl.hpp"
#include <jsonrpc/JsonSerializer.hpp>
#include <KurentoException.hpp>

#define GST_CAT_DEFAULT kurento_nubo_face_detector_impl
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "KurentoNuboFaceDetectorImpl"

#define VIEW_FACES "view-faces"
#define DETECT_BY_EVENT "detect-event"
#define SEND_META_DATA "send-meta-data"

namespace kurento
{
  namespace module
  {
    namespace nubofacedetector
    {

      NuboFaceDetectorImpl::NuboFaceDetectorImpl (const boost::property_tree::ptree &config, std::shared_ptr<MediaPipeline> mediaPipeline)  : FilterImpl (config, std::dynamic_pointer_cast<MediaPipelineImpl> (mediaPipeline)) 
      {
  
	g_object_set (element, "filter-factory", "nubofacedetector", NULL);

	g_object_get (G_OBJECT (element), "filter", &nubo_face, NULL);

	if (NULL == nubo_face) {
	  throw KurentoException (MEDIA_OBJECT_NOT_AVAILABLE,
				  "Media Object not available");
	}

	g_object_unref(nubo_face);

      }

      void NuboFaceDetectorImpl::showFaces(int viewFaces)
      {
	g_object_set(G_OBJECT (nubo_face), VIEW_FACES, viewFaces, NULL);
      }

      void NuboFaceDetectorImpl::detectByEvent(int event)
      {
  
	g_object_set(G_OBJECT (nubo_face), DETECT_BY_EVENT, event, NULL);
  
      }
  
      void NuboFaceDetectorImpl::sendMetaData(int metaData)
      {
	g_object_set(G_OBJECT (nubo_face),SEND_META_DATA , metaData, NULL);
      }

      MediaObjectImpl *
      NuboFaceDetectorImplFactory::createObject (const boost::property_tree::ptree &config, std::shared_ptr<MediaPipeline> mediaPipeline) const
      {

	return new NuboFaceDetectorImpl (config, mediaPipeline);
      }

      NuboFaceDetectorImpl::StaticConstructor NuboFaceDetectorImpl::staticConstructor;

      NuboFaceDetectorImpl::StaticConstructor::StaticConstructor()
      {
	GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
				 GST_DEFAULT_NAME);
      }

    } /* nubofacedetector */
  } /* module */
} /* kurento */
