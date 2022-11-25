// Copyright 2022, Christopher Dilks
// Subject to the terms in the LICENSE file found in the top-level directory.
//
//

#include "IrtGeo.h"

IrtGeo::IrtGeo(std::string detName_, std::string compactFile_, bool verbose_) : detName(detName_), verbose(verbose_) {

  // compact file name; if it's not been specified, try to find the default one
  std::string compactFile;
  if(compactFile_=="") {
    std::string DETECTOR_PATH(getenv("DETECTOR_PATH"));
    std::string DETECTOR_CONFIG(getenv("DETECTOR_CONFIG"));
    if(DETECTOR_PATH.empty() || DETECTOR_CONFIG.empty()) {
      PrintLog(stderr,"ERROR: cannot find default compact file, since env vars DETECTOR_PATH and DETECTOR_CONFIG are not set");
      return;
    }
    compactFile = DETECTOR_PATH + "/" + DETECTOR_CONFIG + ".xml";
  } else compactFile = compactFile_;

  // build DD4hep detector from compact file
  det = &dd4hep::Detector::getInstance();
  det->fromXML(compactFile);

  // set IRT and DD4hep geometry handles
  Bind();
}

// set IRT and DD4hep geometry handles
void IrtGeo::Bind() {
  // DD4hep geometry handles
  detRich = det->detector(detName);
  posRich = detRich.placement().position();
  // IRT geometry handles
  irtGeometry = new CherenkovDetectorCollection();
  irtDetector = irtGeometry->AddNewDetector(detName.c_str());
}

// radiators
const char * IrtGeo::RadiatorName(int num) {
  if(num==kAerogel)  return "Aerogel";
  else if(num==kGas) return "Gas";
  else {
    PrintLog(stderr,"ERROR: unknown radiator number {}",num);
    return "UNKNOWN_RADIATOR";
  }
}
int IrtGeo::RadiatorNum(std::string name) {
  if(name=="Aerogel")  return kAerogel;
  else if(name=="Gas") return kGas;
  else {
    PrintLog(stderr,"ERROR: unknown radiator name {}",name);
    return -1;
  }
}
int IrtGeo::RadiatorNum(const char * name) {
  return RadiatorNum(std::string(name));
}
// dd4hep::DetElement *IrtGeo::RadiatorDetElement(int num) {
//   auto name = RadiatorName(num) + "_de";
//   std::transform(
//       name.begin(), name.end(), name.begin(),
//       [] (auto c) {return std::tolower(c); }
//       );
//   for(auto const& [de_name, det_elem] : detRich.children())
//     if(de_name.find(name)!=std::string::npos)
//       return &det_elem;
//   PrintLog(stderr,"ERROR: cannot find DetElement {}",name);
//   return nullptr;
// }


IrtGeo::~IrtGeo() {
  if(irtDetector) delete irtDetector;
  if(irtGeometry) delete irtGeometry;
}
