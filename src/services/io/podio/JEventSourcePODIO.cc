// Copyright 2022, David Lawrence
// Subject to the terms in the LICENSE file found in the top-level directory.
//
//
// This is a JANA event source that uses PODIO to read from a ROOT
// file created using the EDM4hep Data Model.
//
// This uses the EICRootReader and EICEventStore classes. It is thread safe.

#include "JEventSourcePODIO.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>
#include <filesystem>

#include <JANA/JFactoryGenerator.h>

// podio specific includes
#include "podio/EventStore.h"
#include "podio/IReader.h"
#include "podio/UserDataCollection.h"
#include "podio/podioVersion.h"

// This file is generated automatically by make_datamodel_glue.py
#include "datamodel_glue.h"


//------------------------------------------------------------------------------
// CopyPodioToJEventT
//
/// This is called from the generated code in datamodel_glue.h. This will copy
/// the data objects from the given DataVector into the given JEvent so that
/// users downstream can access them via the standard JANA event.Get<>()
/// mechanism. Ownership of the high-level objects is passed to JANA which
/// will automatically delete them. Ownership of the Obj-level objects are
/// is given to the caller who must take care of deleting them.
///
/// Note that deleting the Obj-level object pointers are also handled automatically
/// since the podio_objs reference passed to us comes from a EICEventStore which
/// is itself managed by JANA.
///
/// \tparam T           podio high-level data type (e.g. edm4hep::EventHeader)
/// \tparam Tobj        podio Obj-level data type (e.g. edm4hep::EventHeaderObj)
/// \tparam Tdata       podio POD-level data tpye (e.g. edm4hep::EventHeaderData)
/// \param dvt          EICEventStore::DataVector containing a vector of the POD-level objects to be copied from
/// \param event        JANA JEvent to copy the data objects into
/// \param podio_objs   caller supplied container to return list of Obj-level object pointers so caller can delete later
//------------------------------------------------------------------------------
template <typename T, typename Tobj, typename Tdata>
void CopyToJEventT(EICEventStore::DataVectorT<Tdata> *dvt, std::shared_ptr<JEvent> &event, std::vector<podio::ObjBase*> &podio_objs){

    // Create high-level podio objects (e.g. edm4hep::EventHeader)
    // n.b. In podio, the data actually resides in a member of the
    // "Obj" clas (e.g. edm4hep::EventHeaderObj). Thus, we must instantiate
    // one of those and then instantiate the high-level object to wrap
    // it. The high-level objects will be passed to JANA to own and manage
    // while the "Obj" objects will be added to the podio_objs vector
    // so that the caller can manage deleting those. (The podio_objs
    // vector is likely a member of an EICEventStore object and all
    // of the objects in podio_objs will be deleted at the end of the
    // event.)
    podio::ObjectID id{0,dvt->collectionID};
    std::vector<const T*> tptrs;
    for( auto &data : dvt->vec ){
        auto obj = new Tobj();
        obj->id = id;
        obj->data = data;
        podio_objs.push_back(obj); // pass ownship of "Obj" objects to caller
        tptrs.push_back( new T(obj) );
        id.index++; // TODO: See above to-do on ObjectIDs
    }
    event->Insert( tptrs, dvt->name ); // pass ownership of high-level objects to JANA
}

//------------------------------------------------------------------------------
// Constructor
//
///
/// \param resource_name  Name of root file to open (n.b. file is not opened until Open() is called)
/// \param app            JApplication
//------------------------------------------------------------------------------
JEventSourcePODIO::JEventSourcePODIO(std::string resource_name, JApplication* app) : JEventSource(resource_name, app) {
    SetTypeName(NAME_OF_THIS); // Provide JANA with class name

    // Tell JANA that we want it to call the FinishEvent() method.
    EnableFinishEvent();
}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
JEventSourcePODIO::~JEventSourcePODIO() {
    LOG << "Closing Event Source for " << GetResourceName() << LOG_END;
}

//------------------------------------------------------------------------------
// Open
//
/// Open the root file and read in metadata.
//------------------------------------------------------------------------------
void JEventSourcePODIO::Open() {

    // Get the list of output collections to include
    // TODO: Convert this to using JANA support of array values in config parameters once it is available.
    japp->SetDefaultParameter("PODIO:INPUT_INCLUDE_COLLECTIONS", m_include_collections_str, "Comma separated list of collection names to read in. If not set, all collections will be read. Use PODIO:INPUT_EXCLUDE_COLLECTIONS to read everything except a selection.");
    if( ! m_include_collections_str.empty() ) {
        std::stringstream ss(m_include_collections_str);
        while (ss.good()) {
            std::string substr;
            getline(ss, substr, ',');
            m_INPUT_INCLUDE_COLLECTIONS.insert(substr);
        }
    }

    // Get the list of output collections to exclude
    // TODO: Convert this to using JANA support of array values in config parameters once it is available.
    japp->SetDefaultParameter("PODIO:INPUT_EXCLUDE_COLLECTIONS", m_exclude_collections_str, "Comma separated list of collection names to not read in.");
    if( ! m_exclude_collections_str.empty() ) {
        std::stringstream ss(m_exclude_collections_str);
        while (ss.good()) {
            std::string substr;
            getline(ss, substr, ',');
            m_INPUT_EXCLUDE_COLLECTIONS.insert(substr);
        }
    }

    // Allow user to specify to recycle events forever
    GetApplication()->SetDefaultParameter("PODIO:RUN_FOREVER", run_forever, "set to true to recycle through events continuously");

    bool print_type_table = false;
    GetApplication()->SetDefaultParameter("PODIO:PRINT_TYPE_TABLE", print_type_table, "Print list of collection names and their types");

    try {
        // Have PODIO reader open file and get the number of events from it.
        reader.OpenFile( GetResourceName() );

        auto version = reader.GetPodioVersion();
        bool version_mismatch = version.major > podio::version::build_version.major;
        version_mismatch |= (version.major == podio::version::build_version.major) && (version.minor>podio::version::build_version.minor);
        if( version_mismatch ){
            LOG_ERROR(default_cerr_logger) << "Mismatch in PODIO versions! " << version << " > " << podio::version::build_version << LOG_END;
            GetApplication()->Quit();
            return;
        }
        LOG << "PODIO version: file=" << version << " (executable=" << podio::version::build_version << ")" << LOG_END;

        Nevents_in_file = reader.GetNumEvents();
        LOG << "Opened PODIO file \"" << GetResourceName() << "\" with " << Nevents_in_file << " events" << LOG_END;

        if( print_type_table ) PrintCollectionTypeTable();

    }catch (std::exception &e ){
        _DBG__;
        LOG_ERROR(default_cerr_logger) << "Problem opening file \"" << GetResourceName() << "\"" << LOG_END;
        LOG_ERROR(default_cerr_logger) << e.what() << LOG_END;
        GetApplication()->Quit();
        return;
    }

    // If user specified which collections to include/exclude then set those branch's status now
    if( !m_INPUT_INCLUDE_COLLECTIONS.empty() ){
        reader.SetBranchStatus("*", false); // turn off all branches
        UInt_t found;
        for( const auto &brname : m_INPUT_INCLUDE_COLLECTIONS) {
            LOG << "Disabling reading of all collections" << LOG_END;
            reader.SetBranchStatus(brname.c_str(), true, &found);
            if( !found ){
                LOG_WARN(default_cerr_logger) << "Collection: " << brname << " not found in root file!" << LOG_END;
            }else{
                LOG << "Enabled read of collection(s): " << brname << "  (" << found << " branches)" << LOG_END;
            }
        }
    }

    // If user specified which collections to include/exclude then set those branch's status now
    if( !m_INPUT_EXCLUDE_COLLECTIONS.empty() ){
        UInt_t found;
        for( const auto &brname : m_INPUT_EXCLUDE_COLLECTIONS) {
            reader.SetBranchStatus(brname.c_str(), false, &found);
            if( !found ) {
                LOG_WARN(default_cerr_logger) << "Collection: " << brname << " not found in root file!" << LOG_END;
            }else{
                LOG << "Disabled read of collection(s): " << brname << "  (" << found << " branches)" << LOG_END;
            }
        }
    }

}

//------------------------------------------------------------------------------
// GetEvent
//
/// Read next event from file and copy its objects into the given JEvent.
///
/// \param event
//------------------------------------------------------------------------------
void JEventSourcePODIO::GetEvent(std::shared_ptr<JEvent> event) {

    /// Calls to GetEvent are synchronized with each other, which means they can
    /// read and write state on the JEventSource without causing race conditions.

    // Check if we have exhausted events from file
    if( Nevents_read >= Nevents_in_file ) {
        if( run_forever ){
            Nevents_read = 0;
        }else{
            reader.CloseFile();
            throw RETURN_STATUS::kNO_MORE_EVENTS;
        }
    }

    // Read the specified event into a new EICEventStore.
    auto es = reader.GetEvent( Nevents_read++ );  // take ownership ....
    event->Insert( es );                          // ... and hand over to JANA

    // TODO: The following could be deferred and done in parallel

//    for(auto dv : es->m_datavectors ) {
//        if( dv->name.find("EcalEndcapNHits") != std::string::npos ) {
//            _DBG_<<" +++ Num EcalEndcapNHits: " << dv->GetVectorSize() << std::endl;
//        }
//    }
//    for(auto dv : es->m_objidvectors ){
//        if( dv->name.find("EcalEndcapNHits#") != std::string::npos ){
//            _DBG_<< dv->name << " --------- " << std::endl;
//            auto *vptr = static_cast<std::vector<podio::ObjectID>*>(dv->GetVectorAddress());
//            for( auto &v : *vptr ){
//                _DBG_<< "index: " << v.index << "  collectionID: " << v.collectionID << std::endl;
//            }
//        }
//    }

    // At this point, the EICEventStore object has a bunch of std:vector objects
    // with the POD edm4hep::*Data types (e.g. edm4hep::EventHeaderData).
    // What we need to do now is copy them into high level data
    // types (e.g. edm4hep::EventHeader) and insert them into the JEvent.
    for( auto dv : es->m_datavectors ){
        CopyToJEvent( dv , event, es->m_podio_objs);
    }

    // Get the EventHeader object which contains the run number and event number
    auto headers = event->Get<edm4hep::EventHeader>("EventHeader");
    for( auto h : headers ){ // should only be one, but this makes it easy
        event->SetEventNumber( h->getEventNumber() );
        event->SetRunNumber( h->getRunNumber() );
    }

}

//------------------------------------------------------------------------------
// FinishEvent
//
/// Get the EICEventStore object from the JEvent and have it delete all of the objects
/// it owns. This technically doesn't need to be done here since JANA calling the
/// EICEventStore destructor will do the same thing. This just frees the memory a little
/// sooner.
///
/// This is called automatically by JANA since EnableFinishEvent() is called in the
/// constructor.
///
/// \param event
//------------------------------------------------------------------------------
void JEventSourcePODIO::FinishEvent(JEvent &event){

    auto es = event.GetSingle<EICEventStore>();
    if( es ) const_cast<EICEventStore*>(es)->Clear(); // Delete all underlying podio "Obj" objects
}

//------------------------------------------------------------------------------
// GetDescription
//------------------------------------------------------------------------------
std::string JEventSourcePODIO::GetDescription() {

    /// GetDescription() helps JANA explain to the user what is going on
    return "PODIO root file (example)";
}

//------------------------------------------------------------------------------
// CheckOpenable
//
/// Return a value from 0-1 indicating probability that this source will be
/// able to read this root file. Currently, it simply checks that the file
/// name contains the string ".root" and if does, returns a small number (0.01).
/// This will need to be made more sophisticated if the alternative root file
/// formats need to be supported by other event sources.
///
/// \param resource_name name of root file to evaluate.
/// \return              value from 0-1 indicating confidence that this source can open the given file
//------------------------------------------------------------------------------
template <>
double JEventSourceGeneratorT<JEventSourcePODIO>::CheckOpenable(std::string resource_name) {

    // If source is a root file, given minimal probability of success so we're chosen
    // only if no other ROOT file sources exist.
    return (resource_name.find(".root") != std::string::npos ) ? 0.01 : 0.0;
}

//------------------------------------------------------------------------------
// PrintCollectionTypeTable
//
/// Print the list of collection names from the currently open file along
/// with their types. This will be called automatically when the file is
/// open if the PODIO:PRINT_TYPE_TABLE variable is set to a non-zero value
//------------------------------------------------------------------------------
void JEventSourcePODIO::PrintCollectionTypeTable(void){

    // First, get maximum length of the collection name strings so
    // we can print nicely aligned columns.
    size_t max_name_len =0;
    size_t max_type_len = 0;
    for( auto dv : reader.GetDataVectors() ){
        max_name_len = std::max( max_name_len, dv->name.length() );
        max_type_len = std::max( max_type_len, dv->name.length() );
    }

    // Print table
    std::cout << std::endl;
    std::cout << "Available Collections" << std::endl;
    std::cout << std::endl;
    std::cout << "Collection Name" << std::string( max_name_len + 2 - std::string("Collection Name").length(), ' ' ) << "Data Type" << std::endl;
    std::cout << std::string(max_name_len, '-') << "  " << std::string(max_name_len, '-') << std::endl;
    for( auto dv : reader.GetDataVectors() ){
        std::cout << dv->name + std::string( max_name_len + 2 - dv->name.length(), ' ' );
        std::cout << dv->className << std::endl;
    }
    std::cout << std::endl;

    // Repeat for the objidvectors
    max_name_len =0;
    max_type_len = 0;
    for( auto dv : reader.GetObjIDVectors() ){
        max_name_len = std::max( max_name_len, dv->name.length() );
        max_type_len = std::max( max_type_len, dv->name.length() );
    }

    // Print table
    std::cout << "ObjID Name" << std::string( max_name_len + 2 - std::string("ObjID Name").length(), ' ' ) << "Data Type" << std::endl;
    std::cout << std::string(max_name_len, '-') << "  " << std::string(max_name_len, '-') << std::endl;
    for( auto dv : reader.GetObjIDVectors() ){
        std::cout << dv->name + std::string( max_name_len + 2 - dv->name.length(), ' ' );
        std::cout << dv->className << std::endl;
    }
    std::cout << std::endl;

}