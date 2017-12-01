/*
 * Your License or Copyright can go here
 */

#include "FFTHDFWriterFilter.h"

#include <QtCore/QDir>

#include "SIMPLib/Common/Constants.h"
#include "SIMPLib/FilterParameters/AbstractFilterParametersReader.h"
#include "SIMPLib/FilterParameters/AbstractFilterParametersWriter.h"
#include "SIMPLib/FilterParameters/BooleanFilterParameter.h"
#include "SIMPLib/FilterParameters/DataArraySelectionFilterParameter.h"
#include "SIMPLib/FilterParameters/H5FilterParametersWriter.h"
#include "SIMPLib/FilterParameters/OutputFileFilterParameter.h"
#include "SIMPLib/FilterParameters/SeparatorFilterParameter.h"
#include "SIMPLib/Geometry/ImageGeom.h"
#include "SIMPLib/Math/SIMPLibMath.h"
#include "SIMPLib/SIMPLibVersion.h"
#include "SIMPLib/SIMPLibVersion.h"

#include "H5Support/HDF5ScopedFileSentinel.h"
#include "H5Support/QH5Lite.h"
#include "H5Support/QH5Utilities.h"

#include "MASSIFUtilities/MASSIFUtilitiesConstants.h"
#include "MASSIFUtilities/MASSIFUtilitiesVersion.h"

#define APPEND_DATA_TRUE 1
#define APPEND_DATA_FALSE 0

namespace Detail
{
class H5GroupAutoCloser
{
public:
  H5GroupAutoCloser(hid_t* groupId)
  : gid(groupId)
  {
  }

  virtual ~H5GroupAutoCloser()
  {
    if(*gid > 0)
    {
      H5Gclose(*gid);
    }
  }

private:
  hid_t* gid;
};
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
FFTHDFWriterFilter::FFTHDFWriterFilter()
: AbstractFilter()
, m_OutputFile("")
, m_WritePipeline(true)
,
    //  m_WriteXdmfFile(true),
    m_AppendToExisting(false)
,
    //---------------------------------------
    m_FeatureIdsArrayPath(SIMPL::Defaults::ImageDataContainerName, SIMPL::Defaults::CellAttributeMatrixName, SIMPL::CellData::FeatureIds)
, m_CellPhasesArrayPath(SIMPL::Defaults::ImageDataContainerName, SIMPL::Defaults::CellAttributeMatrixName, SIMPL::CellData::Phases)
, m_CellEulerAnglesArrayPath(SIMPL::Defaults::ImageDataContainerName, SIMPL::Defaults::CellAttributeMatrixName, SIMPL::CellData::EulerAngles)
, m_FeatureIds(nullptr)
, m_CellPhases(nullptr)
, m_CellEulerAngles(nullptr)
, m_FileId(-1)

{
  setupFilterParameters();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
FFTHDFWriterFilter::~FFTHDFWriterFilter()
{
  closeFile();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::setupFilterParameters()
{
  FilterParameterVector parameters;

  parameters.push_back(OutputFileFilterParameter::New("Output File", "OutputFile", getOutputFile(), FilterParameter::Parameter, SIMPL_BIND_SETTER(FFTHDFWriterFilter, this, OutputFile),
                                                      SIMPL_BIND_GETTER(FFTHDFWriterFilter, this, OutputFile), "*.dream3d", ""));
  //  parameters.push_back(BooleanFilterParameter::New("Write Xdmf File", "WriteXdmfFile", getWriteXdmfFile(), FilterParameter::Parameter, "ParaView Compatible File"));
  //--------------
  parameters.push_back(SeparatorFilterParameter::New("Cell Data", FilterParameter::RequiredArray));
  {
    DataArraySelectionFilterParameter::RequirementType req = DataArraySelectionFilterParameter::CreateRequirement(SIMPL::TypeNames::Int32, 1, AttributeMatrix::Type::Cell, IGeometry::Type::Image);
    parameters.push_back(DataArraySelectionFilterParameter::New("Feature Ids", "FeatureIdsArrayPath", getFeatureIdsArrayPath(), FilterParameter::RequiredArray,
                                                                SIMPL_BIND_SETTER(FFTHDFWriterFilter, this, FeatureIdsArrayPath), SIMPL_BIND_GETTER(FFTHDFWriterFilter, this, FeatureIdsArrayPath),
                                                                req));
  }
  {
    DataArraySelectionFilterParameter::RequirementType req = DataArraySelectionFilterParameter::CreateRequirement(SIMPL::TypeNames::Float, 3, AttributeMatrix::Type::Cell, IGeometry::Type::Image);
    parameters.push_back(DataArraySelectionFilterParameter::New("Euler Angles", "CellEulerAnglesArrayPath", getCellEulerAnglesArrayPath(), FilterParameter::RequiredArray,
                                                                SIMPL_BIND_SETTER(FFTHDFWriterFilter, this, CellEulerAnglesArrayPath),
                                                                SIMPL_BIND_GETTER(FFTHDFWriterFilter, this, CellEulerAnglesArrayPath), req));
  }
  {
    DataArraySelectionFilterParameter::RequirementType req = DataArraySelectionFilterParameter::CreateRequirement(SIMPL::TypeNames::Int32, 1, AttributeMatrix::Type::Cell, IGeometry::Type::Image);
    parameters.push_back(DataArraySelectionFilterParameter::New("Phases", "CellPhasesArrayPath", getCellPhasesArrayPath(), FilterParameter::RequiredArray,
                                                                SIMPL_BIND_SETTER(FFTHDFWriterFilter, this, CellPhasesArrayPath), SIMPL_BIND_GETTER(FFTHDFWriterFilter, this, CellPhasesArrayPath),
                                                                req));
  }

  setFilterParameters(parameters);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::readFilterParameters(AbstractFilterParametersReader* reader, int index)
{
  reader->openFilterGroup(this, index);
  setOutputFile(reader->readString("OutputFile", getOutputFile()));
  //----------------------------
  setCellEulerAnglesArrayPath(reader->readDataArrayPath("CellEulerAnglesArrayPath", getCellEulerAnglesArrayPath()));
  setCellPhasesArrayPath(reader->readDataArrayPath("CellPhasesArrayPath", getCellPhasesArrayPath()));
  setFeatureIdsArrayPath(reader->readDataArrayPath("FeatureIdsArrayPath", getFeatureIdsArrayPath()));

  reader->closeFilterGroup();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::dataCheck()
{
  setErrorCondition(0);
  setWarningCondition(0);
  QString ss;

  getDataContainerArray()->getPrereqGeometryFromDataContainer<ImageGeom, AbstractFilter>(this, getFeatureIdsArrayPath().getDataContainerName());

  if(m_OutputFile.isEmpty() == true)
  {
    setErrorCondition(-10000);
    ss = QObject::tr("The output file must be set");
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
  }

  QFileInfo fi(m_OutputFile);
  QDir parentPath(fi.path());
  if(parentPath.exists() == false)
  {
    setWarningCondition(-1);
    ss = QObject::tr("The directory path for the output file does not exist. DREAM.3D will attempt to create this path during execution of the filter");
    notifyWarningMessage(getHumanLabel(), ss, getWarningCondition());
  }
  if(fi.suffix().compare("") == 0)
  {
    m_OutputFile.append(".dream3d");
  }
  if(fi.baseName().compare("") == 0)
  {
    setErrorCondition(-10001);
    ss = QObject::tr("The output file must have its actual filename set");
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
  }

  QFileInfo dirInfo(fi.path());

  if(dirInfo.isWritable() == false && parentPath.exists() == true)
  {
    setErrorCondition(-10002);
    ss = QObject::tr("The user does not have the proper permissions to write to the output file");
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
  }

  QVector<DataArrayPath> dataArrayPaths;

  QVector<size_t> cDims(1, 1);
  m_FeatureIdsPtr = getDataContainerArray()->getPrereqArrayFromPath<DataArray<int32_t>, AbstractFilter>(this, getFeatureIdsArrayPath(),
                                                                                                        cDims); /* Assigns the shared_ptr<> to an instance variable that is a weak_ptr<> */
  if(nullptr != m_FeatureIdsPtr.lock().get())                                                                   /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_FeatureIds = m_FeatureIdsPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
  if(getErrorCondition() >= 0)
  {
    dataArrayPaths.push_back(getFeatureIdsArrayPath());
  }

  m_CellPhasesPtr = getDataContainerArray()->getPrereqArrayFromPath<DataArray<int32_t>, AbstractFilter>(this, getCellPhasesArrayPath(),
                                                                                                        cDims); /* Assigns the shared_ptr<> to an instance variable that is a weak_ptr<> */
  if(nullptr != m_CellPhasesPtr.lock().get())                                                                   /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_CellPhases = m_CellPhasesPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
  if(getErrorCondition() >= 0)
  {
    dataArrayPaths.push_back(getCellPhasesArrayPath());
  }

  cDims[0] = 3;
  m_CellEulerAnglesPtr = getDataContainerArray()->getPrereqArrayFromPath<DataArray<float>, AbstractFilter>(this, getCellEulerAnglesArrayPath(),
                                                                                                           cDims); /* Assigns the shared_ptr<> to an instance variable that is a weak_ptr<> */
  if(nullptr != m_CellEulerAnglesPtr.lock().get())                                                                 /* Validate the Weak Pointer wraps a non-nullptr pointer to a DataArray<T> object */
  {
    m_CellEulerAngles = m_CellEulerAnglesPtr.lock()->getPointer(0);
  } /* Now assign the raw pointer to data from the DataArray<T> object */
  if(getErrorCondition() >= 0)
  {
    dataArrayPaths.push_back(getCellEulerAnglesArrayPath());
  }

  getDataContainerArray()->validateNumberOfTuples<AbstractFilter>(this, dataArrayPaths);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::preflight()
{
  // These are the REQUIRED lines of CODE to make sure the filter behaves correctly
  setInPreflight(true);
  emit preflightAboutToExecute();
  emit updateFilterParameters(this);
  dataCheck();
  emit preflightExecuted();
  setInPreflight(false);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::execute()
{
  setErrorCondition(0);
  setWarningCondition(0);
  dataCheck();
  if(getErrorCondition() < 0)
  {
    return;
  }

  int err = 0;

  // Make sure any directory path is also available as the user may have just typed
  // in a path without actually creating the full path
  QFileInfo fi(m_OutputFile);
  QString parentPath = fi.path();
  QDir dir;
  if(!dir.mkpath(parentPath))
  {
    QString ss = QObject::tr("Error creating parent path '%1'").arg(parentPath);
    setErrorCondition(-11110);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }

  err = openFile(m_AppendToExisting); // Do NOT append to any existing file
  if(err < 0)
  {
    QString ss = QObject::tr("The HDF5 file could not be opened or created.\n The given filename was:\n\t[%1]").arg(m_OutputFile);
    setErrorCondition(-11112);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }

  // This will make sure if we return early from this method that the HDF5 File is properly closed.
  HDF5ScopedFileSentinel scopedFileSentinel(&m_FileId, true);

  // Create DataContainer group!

  err = H5Utilities::createGroupsFromPath(SIMPL::StringConstants::DataContainerGroupName.toLatin1().data(), m_FileId);
  if(err < 0)
  {
    QString ss = QObject::tr("Error creating HDF5 Group '%1'").arg(SIMPL::StringConstants::DataContainerGroupName);
    setErrorCondition(-60);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }

  hid_t dcaGid = H5Gopen(m_FileId, SIMPL::StringConstants::DataContainerGroupName.toLatin1().data(), H5P_DEFAULT);
  scopedFileSentinel.addGroupId(&dcaGid);

  AttributeMatrix::Pointer attrMat = getDataContainerArray()->getAttributeMatrix(m_FeatureIdsArrayPath);
  QVector<size_t> tDims = attrMat->getTupleDimensions();
  m_FeatureIdsPtr.lock()->writeH5Data(dcaGid, tDims);
  //     H5Lite::writePointerDataset;

  attrMat = getDataContainerArray()->getAttributeMatrix(m_CellPhasesArrayPath);
  tDims = attrMat->getTupleDimensions();
  m_CellPhasesPtr.lock()->writeH5Data(dcaGid, tDims);

  attrMat = getDataContainerArray()->getAttributeMatrix(m_CellEulerAnglesArrayPath);
  tDims = attrMat->getTupleDimensions();
  m_CellEulerAnglesPtr.lock()->writeH5Data(dcaGid, tDims);

  // DataContainer::Pointer dc = getDataContainerArray()->getDataContainer(getFeatureIdsArrayPath().getDataContainerName());

  //      err = dc->writeAttributeMatricesToHDF5(dcaGid);
  //    	if (err < 0)
  //        {
  //        notifyErrorMessage(getHumanLabel(), "Error writing DataContainer AttributeMatrices", -803);
  //     	return;
  //        }

  //        H5Gclose(dcaGid);

  //       dcaGid = -1;

  notifyStatusMessage(getHumanLabel(), "Complete");
}

//--------------------------------------------------------------

void FFTHDFWriterFilter::writeXdmfHeader(QTextStream& xdmf)
{
  xdmf << "<?xml version=\"1.0\"?>"
       << "\n";
  xdmf << "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\"[]>"
       << "\n";
  xdmf << "<Xdmf xmlns:xi=\"http://www.w3.org/2003/XInclude\" Version=\"2.2\">"
       << "\n";
  xdmf << " <Domain>"
       << "\n";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void FFTHDFWriterFilter::writeXdmfFooter(QTextStream& xdmf)
{
  xdmf << " </Domain>"
       << "\n";
  xdmf << "</Xdmf>"
       << "\n";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
hid_t FFTHDFWriterFilter::openFile(bool appendData)
{
  // Try to open a file to append data into
  if(APPEND_DATA_TRUE == appendData)
  {
    m_FileId = QH5Utilities::openFile(m_OutputFile, false);
  }
  // No file was found or we are writing new data only to a clean file
  if(APPEND_DATA_FALSE == appendData || m_FileId < 0)
  {
    m_FileId = QH5Utilities::createFile(m_OutputFile);
  }
  return m_FileId;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
herr_t FFTHDFWriterFilter::closeFile()
{
  // Close the file when we are finished with it
  if(m_FileId > 0)
  {
    return QH5Utilities::closeFile(m_FileId);
  }
  return true;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
AbstractFilter::Pointer FFTHDFWriterFilter::newFilterInstance(bool copyFilterParameters)
{
  FFTHDFWriterFilter::Pointer filter = FFTHDFWriterFilter::New();
  if(true == copyFilterParameters)
  {
    copyFilterParameterInstanceVariables(filter.get());
  }
  return filter;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getCompiledLibraryName()
{
  return MASSIFUtilitiesConstants::MASSIFUtilitiesBaseName;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getBrandingString()
{
  return "FFTHDFWriter";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getFilterVersion()
{
  QString version;
  QTextStream vStream(&version);
  vStream << MASSIFUtilities::Version::Major() << "." << MASSIFUtilities::Version::Minor() << "." << MASSIFUtilities::Version::Patch();
  return version;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getGroupName()
{
  return SIMPL::FilterGroups::Unsupported;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getSubGroupName()
{
  return "FFTHDFWriter";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString FFTHDFWriterFilter::getHumanLabel()
{
  return "FFTHDFWriterFilter";
}
