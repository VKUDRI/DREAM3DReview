/* ============================================================================
 * Copyright (c) 2010, Michael A. Jackson (BlueQuartz Software)
 * Copyright (c) 2010, Dr. Michael A. Groeber (US Air Force Research Laboratories
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of Michael A. Groeber, Michael A. Jackson, the US Air Force,
 * BlueQuartz Software nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  This code was written under United States Air Force Contract number
 *                           FA8650-07-D-5800
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

#include "H5MicReader.h"

#include "H5Support/H5Lite.h"
#include "H5Support/H5Utilities.h"

using namespace H5Support;

#include "EbsdLib/Core/EbsdLibConstants.h"
#include "EbsdLib/Core/EbsdMacros.h"
#include "MicConstants.h"

#if defined(H5Support_NAMESPACE)
using namespace H5Support_NAMESPACE;
#endif

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
H5MicReader::H5MicReader() = default;

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
H5MicReader::~H5MicReader()
{
  deletePointers();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int H5MicReader::readFile()
{
  int err = -1;
  if(m_HDF5Path.empty())
  {
    std::cout << "H5MicReader Error: HDF5 Path is empty." << std::endl;
    return -1;
  }

  hid_t fileId = H5Utilities::openFile(getFileName(), true);
  if(fileId < 0)
  {
    std::stringstream ss;
    ss << "H5MicReader Error: Could not open HDF5 file " << getFileName();
    setErrorMessage(ss.str());
    setErrorCode(-100);
    return -100;
  }

  hid_t gid = H5Gopen(fileId, m_HDF5Path.c_str(), H5P_DEFAULT);
  if(gid < 0)
  {
    err = H5Utilities::closeFile(fileId);
    std::stringstream ss;
    ss << "H5MicReader Error: Could not open path: " << m_HDF5Path;
    setErrorMessage(ss.str());
    setErrorCode(-101);
    return -101;
  }

  // Read all the header information
  // std::cout << "H5MicReader:: reading Header .. " << std::endl;
  err = readHeader(gid);

  // Read and transform data
  // std::cout << "H5MicReader:: Reading Data .. " << std::endl;
  err = readData(gid);

  err = H5Gclose(gid);
  err = H5Utilities::closeFile(fileId);

  return err;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int H5MicReader::readHeaderOnly()
{
  int err = -1;
  if(m_HDF5Path.empty())
  {
    std::cout << "H5MicReader Error: HDF5 Path is empty." << std::endl;
    std::stringstream ss;
    ss << "H5MicReader Error: HDF5 Path is empty.";
    setErrorMessage(ss.str());
    setErrorCode(-102);
    return -102;
  }

  hid_t fileId = H5Utilities::openFile(getFileName(), true);
  if(fileId < 0)
  {
    std::stringstream ss;
    ss << "H5MicReader Error: Could not open HDF5 file " << getFileName();
    setErrorMessage(ss.str());
    setErrorCode(-100);
    return -100;
  }

  hid_t gid = H5Gopen(fileId, m_HDF5Path.c_str(), H5P_DEFAULT);
  if(gid < 0)
  {
    err = H5Utilities::closeFile(fileId);
    std::stringstream ss;
    ss << "H5MicReader Error: Could not open path " << m_HDF5Path;
    setErrorMessage(ss.str());
    setErrorCode(-101);
    return -101;
  }

  // Read all the header information
  // std::cout << "H5MicReader:: reading Header .. " << std::endl;
  err = readHeader(gid);
  err = H5Utilities::closeFile(fileId);
  return err;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int H5MicReader::readHeader(hid_t parId)
{
  int err = -1;
  hid_t gid = H5Gopen(parId, Mic::H5Mic::Header.c_str(), H5P_DEFAULT);
  if(gid < 0)
  {
    std::stringstream ss;
    ss << "H5MicReader Error: Could not open 'Header' Group" << m_HDF5Path;
    setErrorMessage(ss.str());
    setErrorCode(-105);
    return -105;
  }

  READ_EBSD_HEADER_DATA("H5MicReader", MicHeaderEntry<float>, float, XRes, Mic::XRes, gid)
  READ_EBSD_HEADER_DATA("H5MicReader", MicHeaderEntry<float>, float, YRes, Mic::YRes, gid)
  READ_EBSD_HEADER_DATA("H5MicReader", MicHeaderEntry<int>, int, XDim, Mic::XDim, gid)
  READ_EBSD_HEADER_DATA("H5MicReader", MicHeaderEntry<int>, int, YDim, Mic::YDim, gid)

  hid_t phasesGid = H5Gopen(gid, Mic::H5Mic::Phases.c_str(), H5P_DEFAULT);
  if(phasesGid < 0)
  {
    std::cout << "H5MicReader Error: Could not open Header/Phases HDF Group. Is this an older file?" << std::endl;
    H5Gclose(gid);
    return -1;
  }

  std::list<std::string> names;
  err = H5Utilities::getGroupObjects(phasesGid, H5Utilities::CustomHDFDataTypes::Group, names);
  if(err < 0 || names.empty())
  {
    std::cout << "H5MicReader Error: There were no Phase groups present in the HDF5 file" << std::endl;
    H5Gclose(phasesGid);
    H5Gclose(gid);
    return -1;
  }
  m_Phases.clear();
  for(const auto& phaseGroupName : names)
  {
    hid_t pid = H5Gopen(phasesGid, phaseGroupName.c_str(), H5P_DEFAULT);
    MicPhase::Pointer currentPhase = MicPhase::New();

    READ_PHASE_HEADER_DATA("H5MicReader", pid, int, Mic::Phase, PhaseIndex, currentPhase)
    READ_PHASE_HEADER_ARRAY("H5MicReader", pid, float, Mic::LatticeConstants, LatticeConstants, currentPhase)
    READ_PHASE_STRING_DATA("H5MicReader", pid, Mic::BasisAtoms, BasisAtoms, currentPhase)
    READ_PHASE_STRING_DATA("H5MicReader", pid, Mic::Symmetry, Symmetry, currentPhase)

    m_Phases.push_back(currentPhase);
    err = H5Gclose(pid);
  }

  std::string completeHeader;
  err = H5Lite::readStringDataset(gid, Mic::H5Mic::OriginalHeader, completeHeader);
  setOriginalHeader(completeHeader);
  err = H5Gclose(phasesGid);
  err = H5Gclose(gid);
  return err;
}

#define MIC_READER_ALLOCATE_AND_READ(name, type)                                                                                                                                                       \
  if(m_ReadAllArrays == true || m_ArrayNames.find(Mic::name) != m_ArrayNames.end())                                                                                                                    \
  {                                                                                                                                                                                                    \
    type* _##name = allocateArray<type>(totalDataRows);                                                                                                                                                \
    if(nullptr != _##name)                                                                                                                                                                             \
    {                                                                                                                                                                                                  \
      ::memset(_##name, 0, numBytes);                                                                                                                                                                  \
      err = H5Lite::readPointerDataset(gid, Mic::name, _##name);                                                                                                                                       \
    }                                                                                                                                                                                                  \
    set##name##Pointer(_##name);                                                                                                                                                                       \
  }

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int H5MicReader::readData(hid_t parId)
{
  int err = -1;

  // Delete any currently existing pointers
  deletePointers();
  // Initialize new pointers
  size_t totalDataRows = 0;

  size_t xDim = getXDimension();
  size_t yDim = getYDimension();

  if(yDim < 1)
  {
    return -200;
  }

  totalDataRows = xDim * yDim;

  hid_t gid = H5Gopen(parId, Mic::H5Mic::Data.c_str(), H5P_DEFAULT);
  if(gid < 0)
  {
    std::cout << "H5MicReader Error: Could not open 'Data' Group" << std::endl;
    return -1;
  }

  setNumberOfElements(totalDataRows);
  size_t numBytes = totalDataRows * sizeof(float);

  MIC_READER_ALLOCATE_AND_READ(Euler1, float);
  MIC_READER_ALLOCATE_AND_READ(Euler2, float);
  MIC_READER_ALLOCATE_AND_READ(Euler3, float);
  MIC_READER_ALLOCATE_AND_READ(Confidence, float);
  MIC_READER_ALLOCATE_AND_READ(Phase, int);
  MIC_READER_ALLOCATE_AND_READ(X, float);
  MIC_READER_ALLOCATE_AND_READ(Y, float);

  err = H5Gclose(gid);

  return err;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void H5MicReader::setArraysToRead(const std::set<std::string>& names)
{
  m_ArrayNames = names;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void H5MicReader::readAllArrays(bool b)
{
  m_ReadAllArrays = b;
}
