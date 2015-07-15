/* Autogenerated with kurento-module-creator */

#ifndef __NUBO_FACE_DETECTOR_IMPL_HPP__
#define __NUBO_FACE_DETECTOR_IMPL_HPP__

#include "FilterImpl.hpp"
#include "NuboFaceDetector.hpp"
#include <EventHandler.hpp>
#include <boost/property_tree/ptree.hpp>

namespace kurento
{
namespace module
{
namespace nubofacedetector
{
class NuboFaceDetectorImpl;
} /* nubofacedetector */
} /* module */
} /* kurento */

namespace kurento
{
void Serialize (std::shared_ptr<kurento::module::nubofacedetector::NuboFaceDetectorImpl> &object, JsonSerializer &serializer);
} /* kurento */

namespace kurento
{
class MediaPipelineImpl;
} /* kurento */

namespace kurento
{
namespace module
{
namespace nubofacedetector
{

class NuboFaceDetectorImpl : public FilterImpl, public virtual NuboFaceDetector
{

public:

  NuboFaceDetectorImpl (const boost::property_tree::ptree &config, std::shared_ptr<MediaPipeline> mediaPipeline);

  virtual ~NuboFaceDetectorImpl () {};

  void showFaces(int viewFaces);
  void detectByEvent(int event);
  void sendMetaData(int metaData);

  /* Next methods are automatically implemented by code generator */
  virtual bool connect (const std::string &eventType, std::shared_ptr<EventHandler> handler);
  virtual void invoke (std::shared_ptr<MediaObjectImpl> obj,
                       const std::string &methodName, const Json::Value &params,
                       Json::Value &response);

  virtual void Serialize (JsonSerializer &serializer);

private:

  GstElement *nubo_face = NULL;
  class StaticConstructor
  {
  public:
    StaticConstructor();
  };

  static StaticConstructor staticConstructor;

};

} /* nubofacedetector */
} /* module */
} /* kurento */

#endif /*  __NUBO_FACE_DETECTOR_IMPL_HPP__ */