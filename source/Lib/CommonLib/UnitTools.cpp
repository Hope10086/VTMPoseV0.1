/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2022, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     UnitTool.cpp
 *  \brief    defines operations for basic units
 */

#include "UnitTools.h"

#include "dtrace_next.h"

#include "Unit.h"
#include "Slice.h"
#include "Picture.h"

#include <utility>
#include <algorithm>

#if GREEN_METADATA_SEI_ENABLED
#include <fstream>
#endif

#if DATA_DIST_COST
#include <iostream>
#include <fstream>
#endif
// CS tools

#if IDEAL_SITU2||IDEAL_SITU1
static double rad2degree(double x)
{
  double M_PI = 3.14159265358979323846;
  return x * 180 / M_PI;
}

static double degress2rad(double x)
{
  double M_PI = 3.14159265358979323846;
  return x * M_PI / 180;
}
#endif

bool CS::isDualITree( const CodingStructure &cs )
{
  return cs.slice->isIntra() && !cs.pcv->ISingleTree;
}

UnitArea CS::getArea( const CodingStructure &cs, const UnitArea &area, const ChannelType chType )
{
  return isDualITree( cs ) || cs.treeType != TREE_D ? area.singleChan( chType ) : area;
}

void CS::setRefinedMotionField(CodingStructure &cs)
{
  for (CodingUnit *cu : cs.cus)
  {
    for (auto &pu : CU::traversePUs(*cu))
    {
      if (PU::checkDMVRCondition(pu))
      {
        PredictionUnit subPu = pu;
        const int      dy    = std::min<int>(pu.lumaSize().height, DMVR_SUBCU_HEIGHT);
        const int      dx    = std::min<int>(pu.lumaSize().width, DMVR_SUBCU_WIDTH);
        Position       puPos = pu.lumaPos();
        int            num   = 0;

        for (int y = puPos.y; y < (puPos.y + pu.lumaSize().height); y = y + dy)
        {
          for (int x = puPos.x; x < (puPos.x + pu.lumaSize().width); x = x + dx)
          {
            subPu.UnitArea::operator=(UnitArea(pu.chromaFormat, Area(x, y, dx, dy)));
            subPu.mv[0] = pu.mv[0];
            subPu.mv[1] = pu.mv[1];
            subPu.mv[REF_PIC_LIST_0] += pu.mvdL0SubPu[num];
            subPu.mv[REF_PIC_LIST_1] -= pu.mvdL0SubPu[num];
            subPu.mv[REF_PIC_LIST_0].clipToStorageBitDepth();
            subPu.mv[REF_PIC_LIST_1].clipToStorageBitDepth();
            pu.mvdL0SubPu[num].setZero();
            num++;
            PU::spanMotionInfo(subPu);
          }
        }
      }
    }
  }
}
// CU tools

bool CU::getRprScaling( const SPS* sps, const PPS* curPPS, Picture* refPic, int& xScale, int& yScale )
{
  const int subWidthC  = SPS::getWinUnitX(sps->getChromaFormatIdc());
  const int subHeightC = SPS::getWinUnitY(sps->getChromaFormatIdc());

  const Window& curScalingWindow = curPPS->getScalingWindow();

  const int curLeftOffset   = subWidthC * curScalingWindow.getWindowLeftOffset();
  const int curRightOffset  = subWidthC * curScalingWindow.getWindowRightOffset();
  const int curTopOffset    = subHeightC * curScalingWindow.getWindowTopOffset();
  const int curBottomOffset = subHeightC * curScalingWindow.getWindowBottomOffset();

  // Note: 64-bit integers are used for sizes such as to avoid possible overflows in corner cases
  const int64_t curPicScalWinWidth  = curPPS->getPicWidthInLumaSamples() - (curLeftOffset + curRightOffset);
  const int64_t curPicScalWinHeight = curPPS->getPicHeightInLumaSamples() - (curTopOffset + curBottomOffset);

  const Window& refScalingWindow = refPic->getScalingWindow();

  const int refLeftOffset   = subWidthC * refScalingWindow.getWindowLeftOffset();
  const int refRightOffset  = subWidthC * refScalingWindow.getWindowRightOffset();
  const int refTopOffset    = subHeightC * refScalingWindow.getWindowTopOffset();
  const int refBottomOffset = subHeightC * refScalingWindow.getWindowBottomOffset();

  const int64_t refPicScalWinWidth  = refPic->getPicWidthInLumaSamples() - (refLeftOffset + refRightOffset);
  const int64_t refPicScalWinHeight = refPic->getPicHeightInLumaSamples() - (refTopOffset + refBottomOffset);

  CHECK(curPicScalWinWidth * 2 < refPicScalWinWidth,
        "curPicScalWinWidth * 2 shall be greater than or equal to refPicScalWinWidth");
  CHECK(curPicScalWinHeight * 2 < refPicScalWinHeight,
        "curPicScalWinHeight * 2 shall be greater than or equal to refPicScalWinHeight");
  CHECK(curPicScalWinWidth > refPicScalWinWidth * 8,
        "curPicScalWinWidth shall be less than or equal to refPicScalWinWidth * 8");
  CHECK(curPicScalWinHeight > refPicScalWinHeight * 8,
        "curPicScalWinHeight shall be less than or equal to refPicScalWinHeight * 8");

  xScale = (int) (((refPicScalWinWidth << SCALE_RATIO_BITS) + (curPicScalWinWidth >> 1)) / curPicScalWinWidth);
  yScale = (int) (((refPicScalWinHeight << SCALE_RATIO_BITS) + (curPicScalWinHeight >> 1)) / curPicScalWinHeight);

  const int maxPicWidth  = sps->getMaxPicWidthInLumaSamples();    // sps_pic_width_max_in_luma_samples
  const int maxPicHeight = sps->getMaxPicHeightInLumaSamples();   // sps_pic_height_max_in_luma_samples
  const int curPicWidth  = curPPS->getPicWidthInLumaSamples();    // pps_pic_width_in_luma_samples
  const int curPicHeight = curPPS->getPicHeightInLumaSamples();   // pps_pic_height_in_luma_samples

  const int picSizeIncrement = std::max((int) 8, (1 << sps->getLog2MinCodingBlockSize()));   // Max(8, MinCbSizeY)

  CHECK((curPicScalWinWidth * maxPicWidth) < refPicScalWinWidth * (curPicWidth - picSizeIncrement),
        "(curPicScalWinWidth * maxPicWidth) should be greater than or equal to refPicScalWinWidth * (curPicWidth - "
        "picSizeIncrement))");
  CHECK((curPicScalWinHeight * maxPicHeight) < refPicScalWinHeight * (curPicHeight - picSizeIncrement),
        "(curPicScalWinHeight * maxPicHeight) should be greater than or equal to refPicScalWinHeight * (curPicHeight - "
        "picSizeIncrement))");

  CHECK(curLeftOffset < -curPicWidth * 15, "The value of SubWidthC * pps_scaling_win_left_offset shall be greater "
                                           "than or equal to -pps_pic_width_in_luma_samples * 15");
  CHECK(curLeftOffset >= curPicWidth,
        "The value of SubWidthC * pps_scaling_win_left_offset shall be less than pps_pic_width_in_luma_samples");
  CHECK(curRightOffset < -curPicWidth * 15, "The value of SubWidthC * pps_scaling_win_right_offset shall be greater "
                                            "than or equal to -pps_pic_width_in_luma_samples * 15");
  CHECK(curRightOffset >= curPicWidth,
        "The value of SubWidthC * pps_scaling_win_right_offset shall be less than pps_pic_width_in_luma_samples");

  CHECK(curTopOffset < -curPicHeight * 15, "The value of SubHeightC * pps_scaling_win_top_offset shall be greater "
                                           "than or equal to -pps_pic_height_in_luma_samples * 15");
  CHECK(curTopOffset >= curPicHeight,
        "The value of SubHeightC * pps_scaling_win_top_offset shall be less than pps_pic_height_in_luma_samples");
  CHECK(curBottomOffset < (-curPicHeight) * 15, "The value of SubHeightC * pps_scaling_win_bottom_offset shall be "
                                                "greater than or equal to -pps_pic_height_in_luma_samples * 15");
  CHECK(curBottomOffset >= curPicHeight,
        "The value of SubHeightC * pps_scaling_win_bottom_offset shall be less than pps_pic_height_in_luma_samples");

  CHECK(curLeftOffset + curRightOffset < -curPicWidth * 15,
        "The value of SubWidthC * ( pps_scaling_win_left_offset + pps_scaling_win_right_offset ) shall be greater than "
        "or equal to -pps_pic_width_in_luma_samples * 15");
  CHECK(curLeftOffset + curRightOffset >= curPicWidth,
        "The value of SubWidthC * ( pps_scaling_win_left_offset + pps_scaling_win_right_offset ) shall be less than "
        "pps_pic_width_in_luma_samples");
  CHECK(curTopOffset + curBottomOffset < -curPicHeight * 15,
        "The value of SubHeightC * ( pps_scaling_win_top_offset + pps_scaling_win_bottom_offset ) shall be greater "
        "than or equal to -pps_pic_height_in_luma_samples * 15");
  CHECK(curTopOffset + curBottomOffset >= curPicHeight,
        "The value of SubHeightC * ( pps_scaling_win_top_offset + pps_scaling_win_bottom_offset ) shall be less than "
        "pps_pic_height_in_luma_samples");

  return refPic->isRefScaled( curPPS );
}

void CU::checkConformanceILRP(Slice *slice)
{
  const int numRefList = slice->isInterB() ? 2 : 1;

  int currentSubPicIdx = NOT_VALID;

  // derive sub-picture index for the current slice
  for( int subPicIdx = 0; subPicIdx < slice->getPic()->cs->sps->getNumSubPics(); subPicIdx++ )
  {
    if( slice->getPic()->cs->pps->getSubPic( subPicIdx ).getSubPicID() == slice->getSliceSubPicId() )
    {
      currentSubPicIdx = subPicIdx;
      break;
    }
  }

  CHECK( currentSubPicIdx == NOT_VALID, "Sub-picture was not found" );

  if( !slice->getPic()->cs->sps->getSubPicTreatedAsPicFlag( currentSubPicIdx ) )
  {
    return;
  }

  //constraint 1: The picture referred to by each active entry in RefPicList[ 0 ] or RefPicList[ 1 ] has the same subpicture layout as the current picture
  bool isAllRefSameSubpicLayout = true;
  for (int refList = 0; refList < numRefList; refList++) // loop over l0 and l1
  {
    RefPicList  eRefPicList = (refList ? REF_PIC_LIST_1 : REF_PIC_LIST_0);

    for (int refIdx = 0; refIdx < slice->getNumRefIdx(eRefPicList); refIdx++)
    {
      const Picture* refPic = slice->getRefPic( eRefPicList, refIdx );

      if( refPic->subPictures.size() != slice->getPic()->cs->pps->getNumSubPics() )
      {
        isAllRefSameSubpicLayout = false;
        refList = numRefList;
        break;
      }
      else
      {
        for( int i = 0; i < refPic->subPictures.size(); i++ )
        {
          const SubPic& refSubPic = refPic->subPictures[i];
          const SubPic& curSubPic = slice->getPic()->cs->pps->getSubPic( i );

          if( refSubPic.getSubPicWidthInCTUs() != curSubPic.getSubPicWidthInCTUs()
            || refSubPic.getSubPicHeightInCTUs() != curSubPic.getSubPicHeightInCTUs()
            || refSubPic.getSubPicCtuTopLeftX() != curSubPic.getSubPicCtuTopLeftX()
            || refSubPic.getSubPicCtuTopLeftY() != curSubPic.getSubPicCtuTopLeftY()
            || ( refPic->layerId != slice->getPic()->layerId && refSubPic.getSubPicID() != curSubPic.getSubPicID() )
            || refSubPic.getTreatedAsPicFlag() != curSubPic.getTreatedAsPicFlag())
          {
            isAllRefSameSubpicLayout = false;
            refIdx = slice->getNumRefIdx(eRefPicList);
            refList = numRefList;
            break;
          }
        }

        // A picture with different sub-picture ID of the collocated sub-picture cannot be used as an active reference picture in the same layer
        if( refPic->layerId == slice->getPic()->layerId )
        {
          isAllRefSameSubpicLayout = isAllRefSameSubpicLayout && refPic->subPictures[currentSubPicIdx].getSubPicID() == slice->getSliceSubPicId();
        }
      }
    }
  }

  //constraint 2: The picture referred to by each active entry in RefPicList[ 0 ] or RefPicList[ 1 ] is an ILRP for which the value of sps_num_subpics_minus1 is equal to 0
  if (!isAllRefSameSubpicLayout)
  {
    for (int refList = 0; refList < numRefList; refList++) // loop over l0 and l1
    {
      const RefPicList eRefPicList = refList ? REF_PIC_LIST_1 : REF_PIC_LIST_0;
      for (int refIdx = 0; refIdx < slice->getNumRefIdx(eRefPicList); refIdx++)
      {
        const Picture* refPic = slice->getRefPic( eRefPicList, refIdx );
        CHECK( refPic->layerId == slice->getPic()->layerId || refPic->subPictures.size() > 1, "The inter-layer reference shall contain a single subpicture or have same subpicture layout with the current picture" );
      }
    }
  }

  return;
}

bool CU::isSameSlice(const CodingUnit& cu, const CodingUnit& cu2)
{
  return cu.slice->getIndependentSliceIdx() == cu2.slice->getIndependentSliceIdx();
}

bool CU::isSameTile(const CodingUnit& cu, const CodingUnit& cu2)
{
  return cu.tileIdx == cu2.tileIdx;
}

bool CU::isSameSliceAndTile(const CodingUnit& cu, const CodingUnit& cu2)
{
  return ( cu.slice->getIndependentSliceIdx() == cu2.slice->getIndependentSliceIdx() ) && ( cu.tileIdx == cu2.tileIdx );
}

bool CU::isSameSubPic(const CodingUnit& cu, const CodingUnit& cu2)
{
  return (cu.slice->getPPS()->getSubPicFromCU(cu).getSubPicIdx() == cu2.slice->getPPS()->getSubPicFromCU(cu2).getSubPicIdx()) ;
}

bool CU::isSameCtu(const CodingUnit& cu, const CodingUnit& cu2)
{
  const uint32_t ctuSizeBit = floorLog2(cu.cs->sps->getMaxCUWidth());

  Position pos1Ctu(cu.lumaPos().x  >> ctuSizeBit, cu.lumaPos().y  >> ctuSizeBit);
  Position pos2Ctu(cu2.lumaPos().x >> ctuSizeBit, cu2.lumaPos().y >> ctuSizeBit);

  return pos1Ctu.x == pos2Ctu.x && pos1Ctu.y == pos2Ctu.y;
}

bool CU::isLastSubCUOfCtu( const CodingUnit &cu )
{
  const Area cuAreaY = cu.isSepTree() ? Area( recalcPosition( cu.chromaFormat, cu.chType, CHANNEL_TYPE_LUMA, cu.blocks[cu.chType].pos() ), recalcSize( cu.chromaFormat, cu.chType, CHANNEL_TYPE_LUMA, cu.blocks[cu.chType].size() ) ) : (const Area&)cu.Y();


  return ( ( ( ( cuAreaY.x + cuAreaY.width  ) & cu.cs->pcv->maxCUWidthMask  ) == 0 || cuAreaY.x + cuAreaY.width  == cu.cs->pps->getPicWidthInLumaSamples()  ) &&
           ( ( ( cuAreaY.y + cuAreaY.height ) & cu.cs->pcv->maxCUHeightMask ) == 0 || cuAreaY.y + cuAreaY.height == cu.cs->pps->getPicHeightInLumaSamples() ) );
}

uint32_t CU::getCtuAddr( const CodingUnit &cu )
{
  return getCtuAddr( cu.blocks[cu.chType].lumaPos(), *cu.cs->pcv );
}

int CU::predictQP( const CodingUnit& cu, const int prevQP )
{
  const CodingStructure &cs = *cu.cs;

  const uint32_t ctuRsAddr      = getCtuAddr(cu);
  const uint32_t ctuXPosInCtus  = ctuRsAddr % cs.pcv->widthInCtus;
  const uint32_t tileColIdx     = cu.slice->getPPS()->ctuToTileCol(ctuXPosInCtus);
  const uint32_t tileXPosInCtus = cu.slice->getPPS()->getTileColumnBd(tileColIdx);
  if (ctuXPosInCtus == tileXPosInCtus
      && !(cu.blocks[cu.chType].x & (cs.pcv->maxCUWidthMask >> getChannelTypeScaleX(cu.chType, cu.chromaFormat)))
      && !(cu.blocks[cu.chType].y & (cs.pcv->maxCUHeightMask >> getChannelTypeScaleY(cu.chType, cu.chromaFormat)))
      && (cs.getCU(cu.blocks[cu.chType].pos().offset(0, -1), cu.chType) != nullptr)
      && CU::isSameSliceAndTile(*cs.getCU(cu.blocks[cu.chType].pos().offset(0, -1), cu.chType), cu))
  {
    return ( ( cs.getCU( cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType ) )->qp );
  }
  else
  {
    const int a = ( cu.blocks[cu.chType].y & ( cs.pcv->maxCUHeightMask >> getChannelTypeScaleY( cu.chType, cu.chromaFormat ) ) ) ? ( cs.getCU(cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType))->qp : prevQP;
    const int b = ( cu.blocks[cu.chType].x & ( cs.pcv->maxCUWidthMask  >> getChannelTypeScaleX( cu.chType, cu.chromaFormat ) ) ) ? ( cs.getCU(cu.blocks[cu.chType].pos().offset( -1, 0 ), cu.chType))->qp : prevQP;

    return ( a + b + 1 ) >> 1;
  }
}

uint32_t CU::getNumPUs( const CodingUnit& cu )
{
  uint32_t cnt = 0;
  PredictionUnit *pu = cu.firstPU;

  do
  {
    cnt++;
  } while( ( pu != cu.lastPU ) && ( pu = pu->next ) );

  return cnt;
}

void CU::addPUs( CodingUnit& cu )
{
  cu.cs->addPU( CS::getArea( *cu.cs, cu, cu.chType ), cu.chType );
}

void CU::saveMotionForHmvp(const CodingUnit &cu)
{
  if (!cu.geoFlag && !cu.affine && !(CU::isIBC(cu) && cu.lwidth() * cu.lheight() <= 16))
  {
    const PredictionUnit &pu = *cu.firstPU;

    MotionInfo mi = pu.getMotionInfo();

#if GDR_ENABLED
    mi.sourcePos   = pu.lumaPos();
    mi.sourceClean = pu.cs->isClean(mi.sourcePos, CHANNEL_TYPE_LUMA);
#endif
    mi.bcwIdx = mi.interDir == 3 ? cu.bcwIdx : BCW_DEFAULT;

    if (CU::isIBC(cu))
    {
      cu.cs->addMiToLut(cu.cs->motionLut.lutIbc, mi);
    }
    else
    {
      const uint32_t  mask = ~0u << (pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2);
      const CompArea &area = pu.cu->Y();

      if ((((area.x + area.width) ^ area.x) & mask) != 0 && (((area.y + area.height) ^ area.y) & mask) != 0)
      {
        cu.cs->addMiToLut(cu.cs->motionLut.lut, mi);
      }
    }
  }
}

PartSplit CU::getSplitAtDepth( const CodingUnit& cu, const unsigned depth )
{
  if (depth >= cu.depth)
  {
    return CU_DONT_SPLIT;
  }

  const PartSplit cuSplitType = PartSplit( ( cu.splitSeries >> ( depth * SPLIT_DMULT ) ) & SPLIT_MASK );

  if (cuSplitType == CU_QUAD_SPLIT)
  {
    return CU_QUAD_SPLIT;
  }
  else if (cuSplitType == CU_HORZ_SPLIT)
  {
    return CU_HORZ_SPLIT;
  }
  else if (cuSplitType == CU_VERT_SPLIT)
  {
    return CU_VERT_SPLIT;
  }
  else if (cuSplitType == CU_TRIH_SPLIT)
  {
    return CU_TRIH_SPLIT;
  }
  else if (cuSplitType == CU_TRIV_SPLIT)
  {
    return CU_TRIV_SPLIT;
  }
  else
  {
    THROW("Unknown split mode");
    return CU_QUAD_SPLIT;
  }
}

ModeType CU::getModeTypeAtDepth( const CodingUnit& cu, const unsigned depth )
{
  ModeType modeType = ModeType( (cu.modeTypeSeries >> (depth * 3)) & 0x07 );
  CHECK( depth > cu.depth, " depth is wrong" );
  return modeType;
}

bool CU::divideTuInRows( const CodingUnit &cu )
{
  CHECK( cu.ispMode != HOR_INTRA_SUBPARTITIONS && cu.ispMode != VER_INTRA_SUBPARTITIONS, "Intra Subpartitions type not recognized!" );
  return cu.ispMode == HOR_INTRA_SUBPARTITIONS ? true : false;
}

PartSplit CU::getISPType( const CodingUnit &cu, const ComponentID compID )
{
  if( cu.ispMode && isLuma( compID ) )
  {
    const bool tuIsDividedInRows = CU::divideTuInRows( cu );

    return tuIsDividedInRows ? TU_1D_HORZ_SPLIT : TU_1D_VERT_SPLIT;
  }
  return TU_NO_ISP;
}

bool CU::isISPLast( const CodingUnit &cu, const CompArea &tuArea, const ComponentID compID )
{
  const PartSplit partitionType = CU::getISPType(cu, compID);

  const Area originalArea = cu.blocks[compID];
  switch( partitionType )
  {
    case TU_1D_HORZ_SPLIT:
      return tuArea.y + tuArea.height == originalArea.y + originalArea.height;
    case TU_1D_VERT_SPLIT:
      return tuArea.x + tuArea.width == originalArea.x + originalArea.width;
    default:
      THROW( "Unknown ISP processing order type!" );
      return false;
  }
}

bool CU::isISPFirst( const CodingUnit &cu, const CompArea &tuArea, const ComponentID compID )
{
  return tuArea == cu.firstTU->blocks[compID];
}

bool CU::canUseISP( const CodingUnit &cu, const ComponentID compID )
{
  const int width     = cu.blocks[compID].width;
  const int height    = cu.blocks[compID].height;
  const int maxTrSize = cu.cs->sps->getMaxTbSize();
  return CU::canUseISP( width, height, maxTrSize );
}

bool CU::canUseISP( const int width, const int height, const int maxTrSize )
{
  const bool notEnoughSamplesToSplit   = (floorLog2(width) + floorLog2(height) <= (floorLog2(MIN_TB_SIZEY) << 1));
  const bool cuSizeLargerThanMaxTrSize = width > maxTrSize || height > maxTrSize;
  if ( notEnoughSamplesToSplit || cuSizeLargerThanMaxTrSize )
  {
    return false;
  }
  return true;
}

bool CU::canUseLfnstWithISP( const CompArea& cuArea, const ISPType ispSplitType )
{
  if( ispSplitType == NOT_INTRA_SUBPARTITIONS )
  {
    return false;
  }
  const Size tuSize = (ispSplitType == HOR_INTRA_SUBPARTITIONS)
                        ? Size(cuArea.width, CU::getISPSplitDim(cuArea.width, cuArea.height, TU_1D_HORZ_SPLIT))
                        : Size(CU::getISPSplitDim(cuArea.width, cuArea.height, TU_1D_VERT_SPLIT), cuArea.height);

  if( !( tuSize.width >= MIN_TB_SIZEY && tuSize.height >= MIN_TB_SIZEY ) )
  {
    return false;
  }
  return true;
}

bool CU::canUseLfnstWithISP( const CodingUnit& cu, const ChannelType chType )
{
  CHECK( !isLuma( chType ), "Wrong ISP mode!" );
  return CU::canUseLfnstWithISP( cu.blocks[chType == CHANNEL_TYPE_LUMA ? 0 : 1], (ISPType)cu.ispMode );
}

uint32_t CU::getISPSplitDim( const int width, const int height, const PartSplit ispType )
{
  const bool divideTuInRows = ispType == TU_1D_HORZ_SPLIT;

  uint32_t splitDimensionSize;
  uint32_t nonSplitDimensionSize;

  if( divideTuInRows )
  {
    splitDimensionSize    = height;
    nonSplitDimensionSize = width;
  }
  else
  {
    splitDimensionSize    = width;
    nonSplitDimensionSize = height;
  }

  const int divShift = 2;

  const int minNumberOfSamplesPerCu = 1 << (2 * floorLog2(MIN_TB_SIZEY));

  const int factorToMinSamples =
    nonSplitDimensionSize < minNumberOfSamplesPerCu ? minNumberOfSamplesPerCu >> floorLog2(nonSplitDimensionSize) : 1;
  const int partitionSize =
    (splitDimensionSize >> divShift) < factorToMinSamples ? factorToMinSamples : (splitDimensionSize >> divShift);

  CHECK( floorLog2(partitionSize) + floorLog2(nonSplitDimensionSize) < floorLog2(minNumberOfSamplesPerCu), "A partition has less than the minimum amount of samples!" );
  return partitionSize;
}

bool CU::allLumaCBFsAreZero(const CodingUnit& cu)
{
  if (!cu.ispMode)
  {
    return TU::getCbf(*cu.firstTU, COMPONENT_Y) == false;
  }
  else
  {
    const int numTotalTUs = cu.ispMode == HOR_INTRA_SUBPARTITIONS ? cu.lheight() >> floorLog2(cu.firstTU->lheight())
                                                                  : cu.lwidth() >> floorLog2(cu.firstTU->lwidth());
    TransformUnit* tuPtr = cu.firstTU;
    for (int tuIdx = 0; tuIdx < numTotalTUs; tuIdx++)
    {
      if (TU::getCbf(*tuPtr, COMPONENT_Y) == true)
      {
        return false;
      }
      tuPtr = tuPtr->next;
    }
    return true;
  }
}


PUTraverser CU::traversePUs( CodingUnit& cu )
{
  return PUTraverser( cu.firstPU, cu.lastPU->next );
}

TUTraverser CU::traverseTUs( CodingUnit& cu )
{
  return TUTraverser( cu.firstTU, cu.lastTU->next );
}

cPUTraverser CU::traversePUs( const CodingUnit& cu )
{
  return cPUTraverser( cu.firstPU, cu.lastPU->next );
}

cTUTraverser CU::traverseTUs( const CodingUnit& cu )
{
  return cTUTraverser( cu.firstTU, cu.lastTU->next );
}

// PU tools

int PU::getIntraMPMs(const PredictionUnit &pu, unsigned *mpm)
{
  const ChannelType channelType = CHANNEL_TYPE_LUMA;

  int leftIntraDir  = PLANAR_IDX;
  int aboveIntraDir = PLANAR_IDX;

  const CompArea &area  = pu.block(getFirstComponentOfChannel(channelType));
  const Position  posRT = area.topRight();
  const Position  posLB = area.bottomLeft();

  // Get intra direction of left PU
  const PredictionUnit *puLeft = pu.cs->getPURestricted(posLB.offset(-1, 0), pu, channelType);
  if (puLeft && CU::isIntra(*puLeft->cu))
  {
    leftIntraDir = PU::getIntraDirLuma(*puLeft);
  }

  // Get intra direction of above PU
  const PredictionUnit *puAbove = pu.cs->getPURestricted(posRT.offset(0, -1), pu, channelType);
  if (puAbove && CU::isIntra(*puAbove->cu) && CU::isSameCtu(*pu.cu, *puAbove->cu))
  {
    aboveIntraDir = PU::getIntraDirLuma(*puAbove);
  }

  auto wrap = [](const int m)
  {
    const int mod = NUM_INTRA_ANGULAR_MODES - 1;
    return (m - ANGULAR_BASE + mod) % mod + ANGULAR_BASE;
  };

  const int numCands = leftIntraDir == aboveIntraDir ? 1 : 2;

  const int minCandVal = std::min<int>(leftIntraDir, aboveIntraDir);
  const int maxCandVal = std::max<int>(leftIntraDir, aboveIntraDir);

  mpm[0] = PLANAR_IDX;

  if (maxCandVal < ANGULAR_BASE)
  {
    mpm[1] = DC_IDX;
    mpm[2] = VER_IDX;
    mpm[3] = HOR_IDX;
    mpm[4] = VER_IDX - 4;
    mpm[5] = VER_IDX + 4;
  }
  else if (numCands == 1 || minCandVal < ANGULAR_BASE)
  {
    mpm[1] = maxCandVal;
    mpm[2] = wrap(maxCandVal - 1);
    mpm[3] = wrap(maxCandVal + 1);
    mpm[4] = wrap(maxCandVal - 2);
    mpm[5] = wrap(maxCandVal + 2);
  }
  else   // L!=A
  {
    mpm[1] = leftIntraDir;
    mpm[2] = aboveIntraDir;

    if (maxCandVal - minCandVal == 1)
    {
      mpm[3] = wrap(minCandVal - 1);
      mpm[4] = wrap(maxCandVal + 1);
      mpm[5] = wrap(minCandVal - 2);
    }
    else if (maxCandVal - minCandVal >= NUM_INTRA_ANGULAR_MODES - 3)
    {
      mpm[3] = wrap(minCandVal + 1);
      mpm[4] = wrap(maxCandVal - 1);
      mpm[5] = wrap(minCandVal + 2);
    }
    else if (maxCandVal - minCandVal == 2)
    {
      mpm[3] = wrap(minCandVal + 1);
      mpm[4] = wrap(minCandVal - 1);
      mpm[5] = wrap(maxCandVal + 1);
    }
    else
    {
      mpm[3] = wrap(minCandVal - 1);
      mpm[4] = wrap(minCandVal + 1);
      mpm[5] = wrap(maxCandVal - 1);
    }
  }

  for (int i = 0; i < NUM_MOST_PROBABLE_MODES; i++)
  {
    CHECK(mpm[i] >= NUM_LUMA_MODE, "Invalid MPM");
  }

  return numCands;
}

bool PU::isMIP(const PredictionUnit &pu, const ChannelType &chType)
{
  if (chType == CHANNEL_TYPE_LUMA)
  {
    // Default case if chType is omitted.
    return pu.cu->mipFlag;
  }
  else
  {
    return isDMChromaMIP(pu) && (pu.intraDir[CHANNEL_TYPE_CHROMA] == DM_CHROMA_IDX);
  }
}

bool PU::isDMChromaMIP(const PredictionUnit &pu)
{
  return !pu.cu->isSepTree() && (pu.chromaFormat == CHROMA_444) && getCoLocatedLumaPU(pu).cu->mipFlag;
}

uint32_t PU::getIntraDirLuma( const PredictionUnit &pu )
{
  if (isMIP(pu))
  {
    return PLANAR_IDX;
  }
  else
  {
    return pu.intraDir[CHANNEL_TYPE_LUMA];
  }
}


void PU::getIntraChromaCandModes( const PredictionUnit &pu, unsigned modeList[NUM_CHROMA_MODE] )
{
  modeList[0] = PLANAR_IDX;
  modeList[1] = VER_IDX;
  modeList[2] = HOR_IDX;
  modeList[3] = DC_IDX;
  modeList[4] = LM_CHROMA_IDX;
  modeList[5] = MDLM_L_IDX;
  modeList[6] = MDLM_T_IDX;
  modeList[7] = DM_CHROMA_IDX;

  // If Direct Mode is MIP, mode cannot be already in the list.
  if (isDMChromaMIP(pu))
  {
    return;
  }

  const uint32_t lumaMode = getCoLocatedIntraLumaMode(pu);
  for (int i = 0; i < 4; i++)
  {
    if (lumaMode == modeList[i])
    {
      modeList[i] = VDIA_IDX;
      break;
    }
  }
}

bool PU::isLMCMode(unsigned mode)
{
  return (mode >= LM_CHROMA_IDX && mode <= MDLM_T_IDX);
}

bool PU::isLMCModeEnabled(const PredictionUnit &pu, unsigned mode)
{
  if ( pu.cs->sps->getUseLMChroma() && pu.cu->checkCCLMAllowed() )
  {
    return true;
  }
  return false;
}

int PU::getLMSymbolList(const PredictionUnit &pu, int *modeList)
{
  int idx = 0;

  modeList[idx++] = LM_CHROMA_IDX;
  modeList[idx++] = MDLM_L_IDX;
  modeList[idx++] = MDLM_T_IDX;
  return idx;
}

uint32_t PU::getFinalIntraMode( const PredictionUnit &pu, const ChannelType &chType )
{
  uint32_t intraMode = pu.intraDir[chType];

  if (intraMode == DM_CHROMA_IDX && !isLuma(chType))
  {
    intraMode = getCoLocatedIntraLumaMode(pu);
  }
  if (pu.chromaFormat == CHROMA_422 && !isLuma(chType) && intraMode < NUM_LUMA_MODE)   // map directional, planar and dc
  {
    intraMode = g_chroma422IntraAngleMappingTable[intraMode];
  }
  return intraMode;
}

const PredictionUnit &PU::getCoLocatedLumaPU(const PredictionUnit &pu)
{
  Position topLeftPos = pu.blocks[pu.chType].lumaPos();

  Position refPos =
    topLeftPos.offset(pu.blocks[pu.chType].lumaSize().width >> 1, pu.blocks[pu.chType].lumaSize().height >> 1);

  const PredictionUnit &lumaPU = pu.cu->isSepTree() ? *pu.cs->picture->cs->getPU(refPos, CHANNEL_TYPE_LUMA)
                                                    : *pu.cs->getPU(topLeftPos, CHANNEL_TYPE_LUMA);

  return lumaPU;
}

uint32_t PU::getCoLocatedIntraLumaMode(const PredictionUnit &pu)
{
  return PU::getIntraDirLuma(PU::getCoLocatedLumaPU(pu));
}

int PU::getWideAngle( const TransformUnit &tu, const uint32_t dirMode, const ComponentID compID )
{
  //This function returns a wide angle index taking into account that the values 0 and 1 are reserved
  //for Planar and DC respectively, as defined in the Spec. Text.
  if( dirMode < 2 )
  {
    return ( int ) dirMode;
  }

  const CompArea&  area         = tu.cu->ispMode && isLuma(compID) ? tu.cu->blocks[compID] : tu.blocks[ compID ];
  int              width        = area.width;
  int              height       = area.height;
  int              modeShift[ ] = { 0, 6, 10, 12, 14, 15 };
  int              deltaSize    = abs( floorLog2( width ) - floorLog2( height ) );
  int              predMode     = dirMode;

  if( width > height && dirMode < 2 + modeShift[ deltaSize ] )
  {
    predMode += ( VDIA_IDX - 1 );
  }
  else if( height > width && predMode > VDIA_IDX - modeShift[ deltaSize ] )
  {
    predMode -= ( VDIA_IDX + 1 );
  }

  return predMode;
}

bool PU::addMergeHmvpCand(const CodingStructure &cs, MergeCtx &mrgCtx, const int &mrgCandIdx,
                          const uint32_t maxNumMergeCandMin1, int &cnt, const bool isAvailableA1,
                          const MotionInfo &miLeft, const bool isAvailableB1, const MotionInfo &miAbove,
                          const bool ibcFlag, const bool isGt4x4
#if GDR_ENABLED
                          ,
                          const PredictionUnit &pu, bool &allCandSolidInAbove
#endif
)
{
  const Slice &slice = *cs.slice;

  const auto &lut = ibcFlag ? cs.motionLut.lutIbc : cs.motionLut.lut;

  const int numAvailCandInLut = (int) lut.size();

#if GDR_ENABLED
  const bool isEncodeGdrClean =
    cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder
    && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA))
        || cs.picHeader->getNumVerVirtualBoundaries() == 0);

  bool  vbOnCtuBoundary = true;
  if (isEncodeGdrClean)
  {
    vbOnCtuBoundary = (pu.cs->picHeader->getNumVerVirtualBoundaries() == 0) || (pu.cs->picHeader->getVirtualBoundariesPosX(0) % pu.cs->sps->getMaxCUWidth() == 0);
    allCandSolidInAbove = allCandSolidInAbove && vbOnCtuBoundary;
  }
#endif
  for (int mrgIdx = 0; mrgIdx < numAvailCandInLut; mrgIdx++)
  {
    const MotionInfo &miNeighbor = lut[numAvailCandInLut - 1 - mrgIdx];
#if GDR_ENABLED
    Position sourcePos = Position(0, 0);
    if (isEncodeGdrClean)
    {
      sourcePos = miNeighbor.sourcePos;
    }
#endif

    if (mrgIdx > 1 || ((mrgIdx > 0 || !isGt4x4) && ibcFlag)
        || ((!isAvailableA1 || miLeft != miNeighbor) && (!isAvailableB1 || miAbove != miNeighbor)))
    {
      mrgCtx.interDirNeighbours[cnt] = miNeighbor.interDir;
      mrgCtx.useAltHpelIf[cnt]       = !ibcFlag && miNeighbor.useAltHpelIf;
      mrgCtx.bcwIdx[cnt]             = miNeighbor.interDir == 3 ? miNeighbor.bcwIdx : BCW_DEFAULT;

      const int numLists = slice.isInterB() ? 2 : 1;

      for (int listIdx = 0; listIdx < numLists; listIdx++)
      {
        mrgCtx.mvFieldNeighbours[2 * cnt + listIdx].setMvField(miNeighbor.mv[listIdx], miNeighbor.refIdx[listIdx]);

#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          // note : cannot guarantee the order/value in the lut if any of the lut is in dirty area
          mrgCtx.mvPos[2 * cnt + listIdx]   = sourcePos;
          mrgCtx.mvSolid[2 * cnt + listIdx] = allCandSolidInAbove && vbOnCtuBoundary;
          mrgCtx.mvValid[2 * cnt + listIdx] =
            cs.isClean(pu.Y().bottomRight(), miNeighbor.mv[listIdx], RefPicList(listIdx), miNeighbor.refIdx[listIdx]);
          allCandSolidInAbove = allCandSolidInAbove && vbOnCtuBoundary;
        }
#endif
      }

      if (mrgCandIdx == cnt)
      {
        return true;
      }

      if (++cnt == maxNumMergeCandMin1)
      {
        break;
      }
    }
  }

  if (cnt < maxNumMergeCandMin1)
  {
    mrgCtx.useAltHpelIf[cnt] = false;
  }

  return false;
}

void PU::getIBCMergeCandidates(const PredictionUnit &pu, MergeCtx& mrgCtx, const int& mrgCandIdx)
{
  const CodingStructure &cs = *pu.cs;
  const uint32_t maxNumMergeCand = pu.cs->sps->getMaxNumIBCMergeCand();
#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA);
  bool  allCandSolidInAbove = true;
#endif

  for (uint32_t ui = 0; ui < maxNumMergeCand; ++ui)
  {
    mrgCtx.bcwIdx[ui]                           = BCW_DEFAULT;
    mrgCtx.interDirNeighbours[ui]               = 0;
    mrgCtx.mvFieldNeighbours[ui * 2].refIdx     = NOT_VALID;
    mrgCtx.mvFieldNeighbours[ui * 2 + 1].refIdx = NOT_VALID;
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      mrgCtx.mvSolid[(ui << 1) + 0] = true;
      mrgCtx.mvSolid[(ui << 1) + 1] = true;
      mrgCtx.mvValid[(ui << 1) + 0] = true;
      mrgCtx.mvValid[(ui << 1) + 1] = true;
    }
#endif
    mrgCtx.useAltHpelIf[ui] = false;
  }

  mrgCtx.numValidMergeCand = maxNumMergeCand;
  // compute the location of the current PU

  int cnt = 0;

  const Position posRT = pu.Y().topRight();
  const Position posLB = pu.Y().bottomLeft();

  MotionInfo miAbove, miLeft, miAboveLeft, miAboveRight, miBelowLeft;

  //left
  const PredictionUnit* puLeft = cs.getPURestricted(posLB.offset(-1, 0), pu, pu.chType);
  bool isGt4x4 = pu.lwidth() * pu.lheight() > 16;
  const bool isAvailableA1 = puLeft && pu.cu != puLeft->cu && CU::isIBC(*puLeft->cu);
  if (isGt4x4 && isAvailableA1)
  {
    miLeft = puLeft->getMotionInfo(posLB.offset(-1, 0));

    // get Inter Dir
    mrgCtx.interDirNeighbours[cnt] = miLeft.interDir;
    // get Mv from Left
    mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miLeft.mv[0], miLeft.refIdx[0]);
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(posLB.offset(-1, 0), pu.chType);
    }
#endif
    if (mrgCandIdx == cnt)
    {
      return;
    }
    cnt++;
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  // above
  const PredictionUnit *puAbove = cs.getPURestricted(posRT.offset(0, -1), pu, pu.chType);
  bool isAvailableB1 = puAbove && pu.cu != puAbove->cu && CU::isIBC(*puAbove->cu);
  if (isGt4x4 && isAvailableB1)
  {
    miAbove = puAbove->getMotionInfo(posRT.offset(0, -1));

    if (!isAvailableA1 || (miAbove != miLeft))
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miAbove.interDir;
      // get Mv from Above
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miAbove.mv[0], miAbove.refIdx[0]);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(posRT.offset(0, -1), pu.chType);
      }
#endif
      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  if (cnt != maxNumMergeCand)
  {
#if GDR_ENABLED
    bool allCandSolidInAbove = true;
    bool found = addMergeHmvpCand(cs, mrgCtx, mrgCandIdx, maxNumMergeCand, cnt, isAvailableA1, miLeft, isAvailableB1,
                                  miAbove, true, isGt4x4, pu, allCandSolidInAbove);
#else
    bool found = addMergeHmvpCand(cs, mrgCtx, mrgCandIdx, maxNumMergeCand, cnt, isAvailableA1, miLeft, isAvailableB1,
                                  miAbove, true, isGt4x4);
#endif

    if (found)
    {
      return;
    }
  }

  while (cnt < maxNumMergeCand)
  {
    mrgCtx.mvFieldNeighbours[cnt * 2].setMvField(Mv(0, 0), MAX_NUM_REF);
    mrgCtx.interDirNeighbours[cnt] = 1;
#if GDR_ENABLED
    // GDR: zero mv(0,0)
    if (isEncodeGdrClean)
    {
      mrgCtx.mvSolid[cnt << 1] = true && allCandSolidInAbove;
      allCandSolidInAbove      = true && allCandSolidInAbove;
    }
#endif
    if (mrgCandIdx == cnt)
    {
      return;
    }
    cnt++;
  }

  mrgCtx.numValidMergeCand = cnt;
}

void PU::getInterMergeCandidates(const PredictionUnit &pu, MergeCtx &mrgCtx, int mmvdList, const int &mrgCandIdx)
{
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;
  const CodingStructure &cs  = *pu.cs;
  const Slice &slice         = *pu.cs->slice;
  const uint32_t maxNumMergeCand = pu.cs->sps->getMaxNumMergeCand();
  CHECK (maxNumMergeCand > MRG_MAX_NUM_CANDS, "selected maximum number of merge candidate exceeds global limit");
#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool  allCandSolidInAbove = true;
#endif
  for (uint32_t ui = 0; ui < maxNumMergeCand; ++ui)
  {
    mrgCtx.bcwIdx[ui]                              = BCW_DEFAULT;
    mrgCtx.interDirNeighbours[ui]                  = 0;
    mrgCtx.mvFieldNeighbours[(ui << 1)].refIdx     = NOT_VALID;
    mrgCtx.mvFieldNeighbours[(ui << 1) + 1].refIdx = NOT_VALID;
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      mrgCtx.mvSolid[(ui << 1) + 0] = true;
      mrgCtx.mvSolid[(ui << 1) + 1] = true;
      mrgCtx.mvValid[(ui << 1) + 0] = true;
      mrgCtx.mvValid[(ui << 1) + 1] = true;
      mrgCtx.mvPos[(ui << 1) + 0] = Position(0, 0);
      mrgCtx.mvPos[(ui << 1) + 1] = Position(0, 0);
    }
#endif
    mrgCtx.useAltHpelIf[ui] = false;
  }

  mrgCtx.numValidMergeCand = maxNumMergeCand;
  // compute the location of the current PU

  int cnt = 0;

  const Position posLT = pu.Y().topLeft();
  const Position posRT = pu.Y().topRight();
  const Position posLB = pu.Y().bottomLeft();
  MotionInfo miAbove, miLeft, miAboveLeft, miAboveRight, miBelowLeft;

  // above
  const PredictionUnit *puAbove = cs.getPURestricted(posRT.offset(0, -1), pu, pu.chType);

  bool isAvailableB1 = puAbove && isDiffMER(pu.lumaPos(), posRT.offset(0, -1), plevel) && pu.cu != puAbove->cu && CU::isInter(*puAbove->cu);

  if (isAvailableB1)
  {
    miAbove = puAbove->getMotionInfo(posRT.offset(0, -1));

    // get Inter Dir
    mrgCtx.interDirNeighbours[cnt] = miAbove.interDir;
    mrgCtx.useAltHpelIf[cnt] = miAbove.useAltHpelIf;
    // get Mv from Above
    mrgCtx.bcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAbove->cu->bcwIdx : BCW_DEFAULT;
    mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miAbove.mv[0], miAbove.refIdx[0]);

#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      Position pos = puAbove->lumaPos();
      mrgCtx.mvPos[(cnt << 1) + 0] = pos;
      mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(pos, pu.chType);
      mrgCtx.mvValid[(cnt << 1) + 0] = cs.isClean(pu.Y().bottomRight(), miAbove.mv[0], REF_PIC_LIST_0, miAbove.refIdx[0]);
    }
#endif
    if (slice.isInterB())
    {
      mrgCtx.mvFieldNeighbours[(cnt << 1) + 1].setMvField(miAbove.mv[1], miAbove.refIdx[1]);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Position pos = puAbove->lumaPos();
        mrgCtx.mvPos[(cnt << 1) + 1] = pos;
        mrgCtx.mvSolid[(cnt << 1) + 1] = cs.isClean(pos, pu.chType);
        mrgCtx.mvValid[(cnt << 1) + 1] = cs.isClean(pu.Y().bottomRight(), miAbove.mv[1], REF_PIC_LIST_1, miAbove.refIdx[1]);
      }
#endif
    }
    if (mrgCandIdx == cnt)
    {
      return;
    }

    cnt++;
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  //left
  const PredictionUnit* puLeft = cs.getPURestricted(posLB.offset(-1, 0), pu, pu.chType);

  const bool isAvailableA1 = puLeft && isDiffMER(pu.lumaPos(), posLB.offset(-1, 0), plevel) && pu.cu != puLeft->cu && CU::isInter(*puLeft->cu);

  if (isAvailableA1)
  {
    miLeft = puLeft->getMotionInfo(posLB.offset(-1, 0));

    if (!isAvailableB1 || (miAbove != miLeft))
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miLeft.interDir;
      mrgCtx.useAltHpelIf[cnt]       = miLeft.useAltHpelIf;
      mrgCtx.bcwIdx[cnt]             = (mrgCtx.interDirNeighbours[cnt] == 3) ? puLeft->cu->bcwIdx : BCW_DEFAULT;
      // get Mv from Left
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miLeft.mv[0], miLeft.refIdx[0]);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Position pos = puLeft->lumaPos();
        mrgCtx.mvPos[(cnt << 1) + 0] = pos;
        mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(pos, pu.chType);
        mrgCtx.mvValid[(cnt << 1) + 0] = cs.isClean(pu.Y().bottomRight(), miLeft.mv[0], REF_PIC_LIST_0, miLeft.refIdx[0]);
      }
#endif

      if (slice.isInterB())
      {
        mrgCtx.mvFieldNeighbours[(cnt << 1) + 1].setMvField(miLeft.mv[1], miLeft.refIdx[1]);
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Position pos = puLeft->lumaPos();
          mrgCtx.mvPos[(cnt << 1) + 1] = pos;
          mrgCtx.mvSolid[(cnt << 1) + 1] = cs.isClean(pos, pu.chType);
          mrgCtx.mvValid[(cnt << 1) + 1] = cs.isClean(pu.Y().bottomRight(), miLeft.mv[1], REF_PIC_LIST_1, miLeft.refIdx[1]);
        }
#endif
      }
      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }

  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }

  // above right
  const PredictionUnit *puAboveRight = cs.getPURestricted( posRT.offset( 1, -1 ), pu, pu.chType );

  bool isAvailableB0 = puAboveRight && isDiffMER( pu.lumaPos(), posRT.offset(1, -1), plevel) && CU::isInter( *puAboveRight->cu );

  if( isAvailableB0 )
  {
    miAboveRight = puAboveRight->getMotionInfo( posRT.offset( 1, -1 ) );

    if( !isAvailableB1 || ( miAbove != miAboveRight ) )
    {

      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miAboveRight.interDir;
      mrgCtx.useAltHpelIf[cnt] = miAboveRight.useAltHpelIf;
      // get Mv from Above-right
      mrgCtx.bcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAboveRight->cu->bcwIdx : BCW_DEFAULT;
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miAboveRight.mv[0], miAboveRight.refIdx[0] );
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Position pos = puAboveRight->lumaPos();
        mrgCtx.mvPos[(cnt << 1) + 0] = pos;
        mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(pos, pu.chType);
        mrgCtx.mvValid[(cnt << 1) + 0] = cs.isClean(pu.Y().bottomRight(), miAboveRight.mv[0], REF_PIC_LIST_0, miAboveRight.refIdx[0]);
      }
#endif

      if( slice.isInterB() )
      {
        mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miAboveRight.mv[1], miAboveRight.refIdx[1] );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Position pos = puAboveRight->lumaPos();
          mrgCtx.mvPos[(cnt << 1) + 1] = pos;
          mrgCtx.mvSolid[(cnt << 1) + 1] = cs.isClean(pos, pu.chType);
          mrgCtx.mvValid[(cnt << 1) + 1] = cs.isClean(pu.Y().bottomRight(), miAboveRight.mv[1], REF_PIC_LIST_1, miAboveRight.refIdx[1]);
        }
#endif
      }

      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }
  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }

  //left bottom
  const PredictionUnit *puLeftBottom = cs.getPURestricted( posLB.offset( -1, 1 ), pu, pu.chType );

  bool isAvailableA0 = puLeftBottom && isDiffMER( pu.lumaPos(), posLB.offset(-1, 1), plevel) && CU::isInter( *puLeftBottom->cu );

  if( isAvailableA0 )
  {
    miBelowLeft = puLeftBottom->getMotionInfo( posLB.offset( -1, 1 ) );

    if( !isAvailableA1 || ( miBelowLeft != miLeft ) )
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miBelowLeft.interDir;
      mrgCtx.useAltHpelIf[cnt]       = miBelowLeft.useAltHpelIf;
      mrgCtx.bcwIdx[cnt]             = (mrgCtx.interDirNeighbours[cnt] == 3) ? puLeftBottom->cu->bcwIdx : BCW_DEFAULT;
      // get Mv from Bottom-Left
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miBelowLeft.mv[0], miBelowLeft.refIdx[0] );
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Position pos = puLeftBottom->lumaPos();
        mrgCtx.mvPos[(cnt << 1) + 0] = pos;
        mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(pos, pu.chType);
        mrgCtx.mvValid[(cnt << 1) + 0] = cs.isClean(pu.Y().bottomRight(), miBelowLeft.mv[0], REF_PIC_LIST_0, miBelowLeft.refIdx[0]);
      }
#endif

      if( slice.isInterB() )
      {
        mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miBelowLeft.mv[1], miBelowLeft.refIdx[1] );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Position pos = puLeftBottom->lumaPos();
          mrgCtx.mvPos[(cnt << 1) + 1] = pos;
          mrgCtx.mvSolid[(cnt << 1) + 1] = cs.isClean(pos, pu.chType);
          mrgCtx.mvValid[(cnt << 1) + 1] = cs.isClean(pu.Y().bottomRight(), miBelowLeft.mv[1], REF_PIC_LIST_1, miBelowLeft.refIdx[1]);
        }
#endif
      }

      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }
  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }

  // above left
  if ( cnt < 4 )
  {
    const PredictionUnit *puAboveLeft = cs.getPURestricted( posLT.offset( -1, -1 ), pu, pu.chType );

    bool isAvailableB2 = puAboveLeft && isDiffMER( pu.lumaPos(), posLT.offset(-1, -1), plevel ) && CU::isInter( *puAboveLeft->cu );

    if( isAvailableB2 )
    {
      miAboveLeft = puAboveLeft->getMotionInfo( posLT.offset( -1, -1 ) );

      if( ( !isAvailableA1 || ( miLeft != miAboveLeft ) ) && ( !isAvailableB1 || ( miAbove != miAboveLeft ) ) )
      {
        // get Inter Dir
        mrgCtx.interDirNeighbours[cnt] = miAboveLeft.interDir;
        mrgCtx.useAltHpelIf[cnt]       = miAboveLeft.useAltHpelIf;
        mrgCtx.bcwIdx[cnt]             = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAboveLeft->cu->bcwIdx : BCW_DEFAULT;
        // get Mv from Above-Left
        mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miAboveLeft.mv[0], miAboveLeft.refIdx[0] );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Position pos = puAboveLeft->lumaPos();
          mrgCtx.mvPos[(cnt << 1) + 0] = pos;
          mrgCtx.mvSolid[(cnt << 1) + 0] = cs.isClean(pos, pu.chType);
          mrgCtx.mvValid[(cnt << 1) + 0] = cs.isClean(pu.Y().bottomRight(), miAboveLeft.mv[0], REF_PIC_LIST_0, miAboveLeft.refIdx[0]);
        }
#endif

        if( slice.isInterB() )
        {
          mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miAboveLeft.mv[1], miAboveLeft.refIdx[1] );
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            Position pos = puAboveLeft->lumaPos();
            mrgCtx.mvPos[(cnt << 1) + 1] = pos;
            mrgCtx.mvSolid[(cnt << 1) + 1] = cs.isClean(pos, pu.chType);
            mrgCtx.mvValid[(cnt << 1) + 1] = cs.isClean(pu.Y().bottomRight(), miAboveLeft.mv[1], REF_PIC_LIST_1, miAboveLeft.refIdx[1]);
          }
#endif
        }

        if (mrgCandIdx == cnt)
        {
          return;
        }

        cnt++;
      }
    }
  }
  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  if (slice.getPicHeader()->getEnableTMVPFlag() && (pu.lumaSize().width + pu.lumaSize().height > 12))
  {
    //>> MTK colocated-RightBottom
    // offset the pos to be sure to "point" to the same position the uiAbsPartIdx would've pointed to
    Position posRB = pu.Y().bottomRight().offset( -3, -3 );
    const PreCalcValues& pcv = *cs.pcv;
#if GDR_ENABLED
    bool posC0inCurPicSolid = true;
    bool posC1inCurPicSolid = true;
    bool posC0inRefPicSolid = true;
    bool posC1inRefPicSolid = true;
#endif

    Position posC0;
    Position posC1 = pu.Y().center();
    bool C0Avail = false;
    bool boundaryCond = ((posRB.x + pcv.minCUWidth) < pcv.lumaWidth) && ((posRB.y + pcv.minCUHeight) < pcv.lumaHeight);
    const SubPic& curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
    if (curSubPic.getTreatedAsPicFlag())
    {
      boundaryCond = ((posRB.x + pcv.minCUWidth) <= curSubPic.getSubPicRight() &&
                      (posRB.y + pcv.minCUHeight) <= curSubPic.getSubPicBottom());
    }
    if (boundaryCond)
    {
      int posYInCtu = posRB.y & pcv.maxCUHeightMask;
      if (posYInCtu + 4 < pcv.maxCUHeight)
      {
        posC0 = posRB.offset(4, 4);
        C0Avail = true;
      }
    }

    Mv        cColMv;
    int       refIdx      = 0;
    int       dir         = 0;
    unsigned  arrayAddr   = cnt;
    bool      existMV     = (C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, cColMv, refIdx, false))
                   || getColocatedMVP(pu, REF_PIC_LIST_0, posC1, cColMv, refIdx, false);
    if (existMV)
    {
      dir     |= 1;
      mrgCtx.mvFieldNeighbours[2 * arrayAddr].setMvField(cColMv, refIdx);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Mv ccMv;

        posC0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
        posC1inCurPicSolid = cs.isClean(posC1, CHANNEL_TYPE_LUMA);
        posC0inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_0, refIdx);
        posC1inRefPicSolid = cs.isClean(posC1, REF_PIC_LIST_0, refIdx);

        bool isMVP0exist = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, ccMv, refIdx, false);

        Position pos = isMVP0exist ? posC0 : posC1;
        mrgCtx.mvPos[2 * arrayAddr] = pos;
        mrgCtx.mvSolid[2 * arrayAddr] =
          isMVP0exist ? (posC0inCurPicSolid && posC0inRefPicSolid) : (posC1inCurPicSolid && posC1inRefPicSolid);
        mrgCtx.mvValid[2 * arrayAddr] = cs.isClean(pu.Y().bottomRight(), ccMv, REF_PIC_LIST_0, refIdx);
      }
#endif
    }

    if (slice.isInterB())
    {
      existMV = (C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, cColMv, refIdx, false))
                || getColocatedMVP(pu, REF_PIC_LIST_1, posC1, cColMv, refIdx, false);
      if (existMV)
      {
        dir     |= 2;
        mrgCtx.mvFieldNeighbours[2 * arrayAddr + 1].setMvField(cColMv, refIdx);
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Mv ccMv;

          posC0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
          posC1inCurPicSolid = cs.isClean(posC1, CHANNEL_TYPE_LUMA);
          posC0inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_1, refIdx);
          posC1inRefPicSolid = cs.isClean(posC1, REF_PIC_LIST_1, refIdx);

          bool isMVP0exist = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, ccMv, refIdx, false);

          Position pos = isMVP0exist ? posC0 : posC1;
          mrgCtx.mvPos[2 * arrayAddr + 1] = pos;
          mrgCtx.mvSolid[2 * arrayAddr + 1] =
            isMVP0exist ? (posC0inCurPicSolid && posC0inRefPicSolid) : (posC1inCurPicSolid && posC1inRefPicSolid);
          mrgCtx.mvValid[2 * arrayAddr + 1] = cs.isClean(pu.Y().bottomRight(), ccMv, REF_PIC_LIST_1, refIdx);
        }
#endif
      }
    }

    if( dir != 0 )
    {
      bool addTMvp = true;
      if( addTMvp )
      {
        mrgCtx.interDirNeighbours[arrayAddr] = dir;
        mrgCtx.bcwIdx[arrayAddr]             = BCW_DEFAULT;
        mrgCtx.useAltHpelIf[arrayAddr]       = false;
        if (mrgCandIdx == cnt)
        {
          return;
        }

        cnt++;
      }
    }
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  int maxNumMergeCandMin1 = maxNumMergeCand - 1;
  if (cnt != maxNumMergeCandMin1)
  {
    bool isGt4x4 = true;
#if GDR_ENABLED
    allCandSolidInAbove = true;
#endif
#if GDR_ENABLED
    bool found = addMergeHmvpCand(cs, mrgCtx, mrgCandIdx, maxNumMergeCandMin1, cnt, isAvailableA1, miLeft,
                                  isAvailableB1, miAbove, CU::isIBC(*pu.cu), isGt4x4, pu, allCandSolidInAbove);
#else
    bool found = addMergeHmvpCand(cs, mrgCtx, mrgCandIdx, maxNumMergeCandMin1, cnt, isAvailableA1, miLeft,
                                  isAvailableB1, miAbove, CU::isIBC(*pu.cu), isGt4x4);
#endif

    if (found)
    {
      return;
    }
  }

  // pairwise-average candidates
  {
    if (cnt > 1 && cnt < maxNumMergeCand)
    {
      mrgCtx.mvFieldNeighbours[cnt * 2].setMvField( Mv( 0, 0 ), NOT_VALID );
      mrgCtx.mvFieldNeighbours[cnt * 2 + 1].setMvField( Mv( 0, 0 ), NOT_VALID );
      // calculate average MV for L0 and L1 seperately
      unsigned char interDir = 0;

      mrgCtx.useAltHpelIf[cnt] = (mrgCtx.useAltHpelIf[0] == mrgCtx.useAltHpelIf[1]) ? mrgCtx.useAltHpelIf[0] : false;
      for( int refListId = 0; refListId < (slice.isInterB() ? 2 : 1); refListId++ )
      {
        const short refIdxI = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].refIdx;
        const short refIdxJ = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].refIdx;

#if GDR_ENABLED
        // GDR: Pairwise average candidate
        bool mvISolid = isEncodeGdrClean ? mrgCtx.mvSolid[0 * 2 + refListId] : true;
        bool mvJSolid = isEncodeGdrClean ? mrgCtx.mvSolid[1 * 2 + refListId] : true;
        bool mvSolid = true;
#endif
        // both MVs are invalid, skip
        if( (refIdxI == NOT_VALID) && (refIdxJ == NOT_VALID) )
        {
          continue;
        }

        interDir += 1 << refListId;
        // both MVs are valid, average these two MVs
        if( (refIdxI != NOT_VALID) && (refIdxJ != NOT_VALID) )
        {
          const Mv &mvI = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].mv;
          const Mv &mvJ = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].mv;

          // average two MVs
          Mv avgMv = mvI;
          avgMv += mvJ;
          avgMv.roundAffine(1);

          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( avgMv, refIdxI );
#if GDR_ENABLED
          // GDR: Pairwise single I,J candidate
          if (isEncodeGdrClean)
          {
            mvSolid = mvISolid && mvJSolid && allCandSolidInAbove;

            mrgCtx.mvPos[cnt * 2 + refListId] = Position(0, 0);
            mrgCtx.mvSolid[cnt * 2 + refListId] = mvSolid && allCandSolidInAbove;
            mrgCtx.mvValid[cnt * 2 + refListId] = cs.isClean(pu.Y().bottomRight(), avgMv, (RefPicList)refListId, refIdxI);
            allCandSolidInAbove = mvSolid && allCandSolidInAbove;
          }
#endif
        }
        // only one MV is valid, take the only one MV
        else if( refIdxI != NOT_VALID )
        {
          Mv singleMv = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].mv;
          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( singleMv, refIdxI );
#if GDR_ENABLED
          // GDR: Pairwise single I,J candidate
          if (isEncodeGdrClean)
          {
            mvSolid = mvISolid && allCandSolidInAbove;

            mrgCtx.mvPos[cnt * 2 + refListId] = Position(0, 0);
            mrgCtx.mvSolid[cnt * 2 + refListId] = mvSolid && allCandSolidInAbove;
            mrgCtx.mvValid[cnt * 2 + refListId] = cs.isClean(pu.Y().bottomRight(), singleMv, (RefPicList)refListId, refIdxI);
            allCandSolidInAbove = mvSolid && allCandSolidInAbove;
          }
#endif
        }
        else if( refIdxJ != NOT_VALID )
        {
          Mv singleMv = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].mv;
          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( singleMv, refIdxJ );
#if GDR_ENABLED
          // GDR: Pairwise single I,J candidate
          if (isEncodeGdrClean)
          {
            mvSolid = mvJSolid && allCandSolidInAbove;

            mrgCtx.mvPos[cnt * 2 + refListId] = Position(0, 0);
            mrgCtx.mvSolid[cnt * 2 + refListId] = mvSolid && allCandSolidInAbove;
            mrgCtx.mvValid[cnt * 2 + refListId] = cs.isClean(pu.Y().bottomRight(), singleMv, (RefPicList)refListId, refIdxJ);
            allCandSolidInAbove = mvSolid && allCandSolidInAbove;
          }
#endif
        }
      }

      mrgCtx.interDirNeighbours[cnt] = interDir;
      if( interDir > 0 )
      {
        cnt++;
      }
    }

    // early termination
    if( cnt == maxNumMergeCand )
    {
      return;
    }
  }

  uint32_t arrayAddr = cnt;

  int numRefIdx = slice.isInterB() ? std::min(slice.getNumRefIdx(REF_PIC_LIST_0), slice.getNumRefIdx(REF_PIC_LIST_1))
                                   : slice.getNumRefIdx(REF_PIC_LIST_0);

  int r = 0;
  int refcnt = 0;
  while (arrayAddr < maxNumMergeCand)
  {
    mrgCtx.interDirNeighbours[arrayAddr] = 1;
    mrgCtx.bcwIdx[arrayAddr]             = BCW_DEFAULT;
    mrgCtx.mvFieldNeighbours[arrayAddr << 1].setMvField(Mv(0, 0), r);
    mrgCtx.useAltHpelIf[arrayAddr] = false;

#if GDR_ENABLED
    // GDR: zero mv(0,0)
    if (isEncodeGdrClean)
    {
      mrgCtx.mvPos[arrayAddr << 1]   = Position(0, 0);
      mrgCtx.mvSolid[arrayAddr << 1] = true && allCandSolidInAbove;
      mrgCtx.mvValid[arrayAddr << 1] = cs.isClean(pu.Y().bottomRight(), Mv(0, 0), REF_PIC_LIST_0, r);
      allCandSolidInAbove = true && allCandSolidInAbove;
    }
#endif
    if (slice.isInterB())
    {
      mrgCtx.interDirNeighbours[arrayAddr] = 3;
      mrgCtx.mvFieldNeighbours[(arrayAddr << 1) + 1].setMvField(Mv(0, 0), r);
#if GDR_ENABLED
      // GDR: zero mv(0,0)
      if (isEncodeGdrClean)
      {
        mrgCtx.mvPos[(arrayAddr << 1) + 1]   = Position(0, 0);
        mrgCtx.mvSolid[(arrayAddr << 1) + 1] = true && allCandSolidInAbove;
        mrgCtx.mvValid[(arrayAddr << 1) + 1] =
          cs.isClean(pu.Y().bottomRight(), Mv(0, 0), (RefPicList) REF_PIC_LIST_1, r);
        allCandSolidInAbove = true && allCandSolidInAbove;
      }
#endif
    }

    arrayAddr++;

    if (refcnt == numRefIdx - 1)
    {
      r = 0;
    }
    else
    {
      ++r;
      ++refcnt;
    }
  }
  mrgCtx.numValidMergeCand = arrayAddr;
}

bool PU::checkDMVRCondition(const PredictionUnit& pu)
{
  if (pu.cs->sps->getUseDMVR() && !pu.cs->picHeader->getDmvrDisabledFlag())
  {
    const int refIdx0 = pu.refIdx[REF_PIC_LIST_0];
    const int refIdx1 = pu.refIdx[REF_PIC_LIST_1];

    const bool ref0IsScaled = refIdx0 < 0 || refIdx0 >= MAX_NUM_REF
                                ? false
                                : pu.cu->slice->getRefPic(REF_PIC_LIST_0, refIdx0)->isRefScaled(pu.cs->pps);
    const bool ref1IsScaled = refIdx1 < 0 || refIdx1 >= MAX_NUM_REF
                                ? false
                                : pu.cu->slice->getRefPic(REF_PIC_LIST_1, refIdx1)->isRefScaled(pu.cs->pps);

    return pu.mergeFlag && pu.mergeType == MRG_TYPE_DEFAULT_N && !pu.ciipFlag && !pu.cu->affine && !pu.mmvdMergeFlag
           && !pu.cu->mmvdSkip && PU::isSimpleSymmetricBiPred(pu) && PU::dmvrBdofSizeCheck(pu) && !ref0IsScaled
           && !ref1IsScaled;
  }
  else
  {
    return false;
  }
}

static int xGetDistScaleFactor(const int &currPoc, const int &currRefPoc, const int &colPoc, const int &colRefPoc)
{
  const int diffPocD = colPoc - colRefPoc;
  const int diffPocB = currPoc - currRefPoc;

  if (diffPocD == diffPocB)
  {
    return 4096;
  }
  else
  {
    const int tdB   = Clip3(-128, 127, diffPocB);
    const int tdD   = Clip3(-128, 127, diffPocD);
    const int x     = (0x4000 + abs(tdD / 2)) / tdD;
    const int scale = Clip3(-4096, 4095, (tdB * x + 32) >> 6);
    return scale;
  }
}

int convertMvFixedToFloat(int32_t val)
{
  const int sign  = val >> 31;
  const int scale = floorLog2((val ^ sign) | MV_MANTISSA_UPPER_LIMIT) - (MV_MANTISSA_BITCOUNT - 1);

  int exponent;
  int mantissa;
  if (scale >= 0)
  {
    int round = (1 << scale) >> 1;
    int n     = (val + round) >> scale;
    exponent  = scale + ((n ^ sign) >> (MV_MANTISSA_BITCOUNT - 1));
    mantissa  = (n & MV_MANTISSA_UPPER_LIMIT) | (sign * (1 << (MV_MANTISSA_BITCOUNT - 1)));
  }
  else
  {
    exponent = 0;
    mantissa = val;
  }

  return exponent | (mantissa * (1 << MV_EXPONENT_BITCOUNT));
}

int convertMvFloatToFixed(int val)
{
  const int exponent = val & MV_EXPONENT_MASK;
  const int mantissa = val >> MV_EXPONENT_BITCOUNT;
  return exponent == 0 ? mantissa : (mantissa ^ MV_MANTISSA_LIMIT) * (1 << (exponent - 1));
}

int roundMvComp(int x)
{
  return convertMvFloatToFixed(convertMvFixedToFloat(x));
}

int PU::getDistScaleFactor(const int &currPOC, const int &currRefPOC, const int &colPOC, const int &colRefPOC)
{
  return xGetDistScaleFactor(currPOC, currRefPOC, colPOC, colRefPOC);
}

void PU::getInterMMVDMergeCandidates(const PredictionUnit &pu, MergeCtx& mrgCtx, const int& mrgCandIdx)
{
  int refIdxList0, refIdxList1;
  int k;
  int currBaseNum = 0;
  const uint16_t maxNumMergeCand = mrgCtx.numValidMergeCand;

#if GDR_ENABLED
  const CodingStructure &cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif

#if GDR_ENABLED
  for (int k = 0; k < MMVD_BASE_MV_NUM; k++)
  {
    mrgCtx.mmvdSolid[k][0] = true;
    mrgCtx.mmvdSolid[k][1] = true;
    mrgCtx.mmvdValid[k][0] = true;
    mrgCtx.mmvdValid[k][1] = true;
  }
#endif
  for (k = 0; k < maxNumMergeCand; k++)
  {
    refIdxList0 = mrgCtx.mvFieldNeighbours[(k << 1)].refIdx;
    refIdxList1 = mrgCtx.mvFieldNeighbours[(k << 1) + 1].refIdx;

    if ((refIdxList0 >= 0) && (refIdxList1 >= 0))
    {
      mrgCtx.mmvdBaseMv[currBaseNum][0] = mrgCtx.mvFieldNeighbours[(k << 1)];
      mrgCtx.mmvdBaseMv[currBaseNum][1] = mrgCtx.mvFieldNeighbours[(k << 1) + 1];
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        mrgCtx.mmvdSolid[currBaseNum][0] = mrgCtx.mvSolid[(k << 1) + 0];
        mrgCtx.mmvdSolid[currBaseNum][1] = mrgCtx.mvSolid[(k << 1) + 1];
      }
#endif
    }
    else if (refIdxList0 >= 0)
    {
      mrgCtx.mmvdBaseMv[currBaseNum][0] = mrgCtx.mvFieldNeighbours[(k << 1)];
      mrgCtx.mmvdBaseMv[currBaseNum][1] = MvField(Mv(0, 0), -1);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        mrgCtx.mmvdSolid[currBaseNum][0] = mrgCtx.mvSolid[(k << 1) + 0];
        mrgCtx.mmvdSolid[currBaseNum][1] = true;
      }
#endif
    }
    else if (refIdxList1 >= 0)
    {
      mrgCtx.mmvdBaseMv[currBaseNum][0] = MvField(Mv(0, 0), -1);
      mrgCtx.mmvdBaseMv[currBaseNum][1] = mrgCtx.mvFieldNeighbours[(k << 1) + 1];
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        mrgCtx.mmvdSolid[currBaseNum][0] = true;
        mrgCtx.mmvdSolid[currBaseNum][1] = mrgCtx.mvSolid[(k << 1) + 1];
      }
#endif
    }
    mrgCtx.mmvdUseAltHpelIf[currBaseNum] = mrgCtx.useAltHpelIf[k];

    currBaseNum++;

    if (currBaseNum == MMVD_BASE_MV_NUM)
    {
      break;
    }
  }
}

bool PU::getColocatedMVP(const PredictionUnit &pu, const RefPicList &eRefPicList, const Position &_pos, Mv& rcMv, const int &refIdx, bool sbFlag)
{
  // don't perform MV compression when generally disabled or subPuMvp is used
  const unsigned scale = 4 * std::max<int>(1, 4 * AMVP_DECIMATION_FACTOR / 4);
  const unsigned mask  = ~( scale - 1 );

  const Position pos = Position{ PosType( _pos.x & mask ), PosType( _pos.y & mask ) };

  const Slice &slice = *pu.cs->slice;

  // use coldir.
  const Picture* const pColPic = slice.getRefPic(RefPicList(slice.isInterB() ? 1 - slice.getColFromL0Flag() : 0), slice.getColRefIdx());

  if( !pColPic )
  {
    return false;
  }

  // Check the position of colocated block is within a subpicture
  const SubPic &curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
  if (curSubPic.getTreatedAsPicFlag())
  {
    if (!curSubPic.isContainingPos(pos))
    {
      return false;
    }
  }
  RefPicList eColRefPicList = slice.getCheckLDC() ? eRefPicList : RefPicList(slice.getColFromL0Flag());

  const MotionInfo& mi = pColPic->cs->getMotionInfo( pos );

  if( !mi.isInter )
  {
    return false;
  }
  if (mi.isIBCmot)
  {
    return false;
  }
  if (CU::isIBC(*pu.cu))
  {
    return false;
  }
  int colRefIdx = mi.refIdx[eColRefPicList];

  if (sbFlag && !slice.getCheckLDC())
  {
    eColRefPicList = eRefPicList;
    colRefIdx      = mi.refIdx[eColRefPicList];
    if (colRefIdx < 0)
    {
      return false;
    }
  }
  else
  {
    if (colRefIdx < 0)
    {
      eColRefPicList = RefPicList(1 - eColRefPicList);
      colRefIdx      = mi.refIdx[eColRefPicList];

      if (colRefIdx < 0)
      {
        return false;
      }
    }
  }

  const Slice *pColSlice = nullptr;

  for( const auto s : pColPic->slices )
  {
    if( s->getIndependentSliceIdx() == mi.sliceIdx )
    {
      pColSlice = s;
      break;
    }
  }

  CHECK( pColSlice == nullptr, "Slice segment not found" );

  const Slice &colSlice = *pColSlice;

  const bool isCurrRefLongTerm = slice.getRefPic(eRefPicList, refIdx)->longTerm;
  const bool isColRefLongTerm  = colSlice.getIsUsedAsLongTerm(eColRefPicList, colRefIdx);

  if (isCurrRefLongTerm != isColRefLongTerm)
  {
    return false;
  }


  // Scale the vector.
  Mv cColMv = mi.mv[eColRefPicList];
  cColMv.setHor(roundMvComp(cColMv.getHor()));
  cColMv.setVer(roundMvComp(cColMv.getVer()));

  if (isCurrRefLongTerm /*|| isColRefLongTerm*/)
  {
    rcMv = cColMv;
    rcMv.clipToStorageBitDepth();
  }
  else
  {
    const int currPOC    = slice.getPOC();
    const int colPOC     = colSlice.getPOC();
    const int colRefPOC  = colSlice.getRefPOC(eColRefPicList, colRefIdx);
    const int currRefPOC = slice.getRefPic(eRefPicList, refIdx)->getPOC();
    const int distscale  = xGetDistScaleFactor(currPOC, currRefPOC, colPOC, colRefPOC);

    if (distscale == 4096)
    {
      rcMv = cColMv;
      rcMv.clipToStorageBitDepth();
    }
    else
    {
      rcMv = cColMv.scaleMv(distscale);
    }
  }

  return true;
}

bool PU::isDiffMER(const Position &pos1, const Position &pos2, const unsigned plevel)
{
  const unsigned xN = pos1.x;
  const unsigned yN = pos1.y;
  const unsigned xP = pos2.x;
  const unsigned yP = pos2.y;

  if ((xN >> plevel) != (xP >> plevel))
  {
    return true;
  }

  if ((yN >> plevel) != (yP >> plevel))
  {
    return true;
  }

  return false;
}

bool PU::addNeighborMv(const Mv& currMv, static_vector<Mv, IBC_NUM_CANDIDATES>& neighborMvs)
{
  for (const auto& cand : neighborMvs)
  {
    if (currMv == cand)
    {
      return false;
    }
  }
  neighborMvs.push_back(currMv);
  return true;
}

void PU::getIbcMVPsEncOnly(PredictionUnit &pu, static_vector<Mv, IBC_NUM_CANDIDATES>& mvPred)
{
  const PreCalcValues   &pcv = *pu.cs->pcv;
  const int  cuWidth = pu.blocks[COMPONENT_Y].width;
  const int  cuHeight = pu.blocks[COMPONENT_Y].height;
  const int  log2UnitWidth = floorLog2(pcv.minCUWidth);
  const int  log2UnitHeight = floorLog2(pcv.minCUHeight);
  const int  totalAboveUnits = (cuWidth >> log2UnitWidth) + 1;
  const int  totalLeftUnits = (cuHeight >> log2UnitHeight) + 1;

  Position posLT = pu.Y().topLeft();

  // above-left
  const PredictionUnit *aboveLeftPU = pu.cs->getPURestricted(posLT.offset(-1, -1), pu, CHANNEL_TYPE_LUMA);
  if (aboveLeftPU && CU::isIBC(*aboveLeftPU->cu))
  {
    addNeighborMv(aboveLeftPU->bv, mvPred);
  }

  // above neighbors
  for (uint32_t dx = 0; dx < totalAboveUnits && mvPred.size() < mvPred.max_size(); dx++)
  {
    const PredictionUnit* tmpPU = pu.cs->getPURestricted(posLT.offset((dx << log2UnitWidth), -1), pu, CHANNEL_TYPE_LUMA);
    if (tmpPU && CU::isIBC(*tmpPU->cu))
    {
      addNeighborMv(tmpPU->bv, mvPred);
    }
  }

  // left neighbors
  for (uint32_t dy = 0; dy < totalLeftUnits && mvPred.size() < mvPred.max_size(); dy++)
  {
    const PredictionUnit* tmpPU = pu.cs->getPURestricted(posLT.offset(-1, (dy << log2UnitHeight)), pu, CHANNEL_TYPE_LUMA);
    if (tmpPU && CU::isIBC(*tmpPU->cu))
    {
      addNeighborMv(tmpPU->bv, mvPred);
    }
  }

  size_t numAvaiCandInLUT = pu.cs->motionLut.lutIbc.size();
  for (uint32_t cand = 0; cand < numAvaiCandInLUT && mvPred.size() < mvPred.max_size(); cand++)
  {
    MotionInfo neibMi = pu.cs->motionLut.lutIbc[cand];
    addNeighborMv(neibMi.bv, mvPred);
  }

  std::array<bool, IBC_NUM_CANDIDATES> isBvCandDerived;
  isBvCandDerived.fill(false);

  auto curNbPred = mvPred.size();
  if (curNbPred < mvPred.max_size())
  {
    do
    {
      curNbPred = mvPred.size();
      for (uint32_t idx = 0; idx < curNbPred && mvPred.size() < mvPred.max_size(); idx++)
      {
        if (!isBvCandDerived[idx])
        {
          Mv derivedBv;
          if (getDerivedBV(pu, mvPred[idx], derivedBv))
          {
            addNeighborMv(derivedBv, mvPred);
          }
          isBvCandDerived[idx] = true;
        }
      }
    } while (mvPred.size() > curNbPred && mvPred.size() < mvPred.max_size());
  }
}

bool PU::getDerivedBV(PredictionUnit &pu, const Mv& currentMv, Mv& derivedMv)
{
  int   cuPelX = pu.lumaPos().x;
  int   cuPelY = pu.lumaPos().y;
  int rX = cuPelX + currentMv.getHor();
  int rY = cuPelY + currentMv.getVer();
  int offsetX = currentMv.getHor();
  int offsetY = currentMv.getVer();

  if( rX < 0 || rY < 0 || rX >= pu.cs->slice->getPPS()->getPicWidthInLumaSamples() || rY >= pu.cs->slice->getPPS()->getPicHeightInLumaSamples() )
  {
    return false;
  }

  const PredictionUnit *neibRefPU = nullptr;
  neibRefPU = pu.cs->getPURestricted(pu.lumaPos().offset(offsetX, offsetY), pu, CHANNEL_TYPE_LUMA);

  bool isIBC = (neibRefPU) ? CU::isIBC(*neibRefPU->cu) : 0;
  if (isIBC)
  {
    derivedMv = neibRefPU->bv;
    derivedMv += currentMv;
  }
  return isIBC;
}

/**
 * Constructs a list of candidates for IBC AMVP (See specification, section "Derivation process for motion vector predictor candidates")
 */
void PU::fillIBCMvpCand(PredictionUnit &pu, AMVPInfo &amvpInfo)
{
  AMVPInfo *pInfo = &amvpInfo;

  pInfo->numCand = 0;

  MergeCtx mergeCtx;
  PU::getIBCMergeCandidates(pu, mergeCtx, AMVP_MAX_NUM_CANDS - 1);
  int candIdx = 0;
  while (pInfo->numCand < AMVP_MAX_NUM_CANDS)
  {
    pInfo->mvCand[pInfo->numCand] = mergeCtx.mvFieldNeighbours[(candIdx << 1) + 0].mv;;
    pInfo->numCand++;
    candIdx++;
  }

  for (Mv &mv : pInfo->mvCand)
  {
    mv.roundIbcPrecInternal2Amvr(pu.cu->imv);
  }
}

/** Constructs a list of candidates for AMVP (See specification, section "Derivation process for motion vector predictor
 * candidates") \param uiPartIdx \param uiPartAddr \param eRefPicList \param refIdx \param pInfo
 */
void PU::fillMvpCand(PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AMVPInfo &amvpInfo)
{
  CodingStructure &cs = *pu.cs;

  AMVPInfo *pInfo = &amvpInfo;

  pInfo->numCand = 0;

  if (refIdx < 0)
  {
    return;
  }

  //-- Get Spatial MV
  Position posLT = pu.Y().topLeft();
  Position posRT = pu.Y().topRight();
  Position posLB = pu.Y().bottomLeft();

#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool &allCandSolidInAbove = amvpInfo.allCandSolidInAbove;
#endif
  {
    bool added = addMVPCandUnscaled(pu, eRefPicList, refIdx, posLB, MD_BELOW_LEFT, *pInfo);

    if (!added)
    {
      added = addMVPCandUnscaled(pu, eRefPicList, refIdx, posLB, MD_LEFT, *pInfo);
    }
  }

  // Above predictor search
  {
    bool added = addMVPCandUnscaled(pu, eRefPicList, refIdx, posRT, MD_ABOVE_RIGHT, *pInfo);

    if (!added)
    {
      added = addMVPCandUnscaled(pu, eRefPicList, refIdx, posRT, MD_ABOVE, *pInfo);

      if (!added)
      {
        addMVPCandUnscaled( pu, eRefPicList, refIdx, posLT, MD_ABOVE_LEFT, *pInfo );
      }
    }
  }

  for( int i = 0; i < pInfo->numCand; i++ )
  {
    pInfo->mvCand[i].roundTransPrecInternal2Amvr(pu.cu->imv);
  }

  if( pInfo->numCand == 2 )
  {
    if( pInfo->mvCand[0] == pInfo->mvCand[1] )
    {
      pInfo->numCand = 1;
    }
  }

  if (cs.picHeader->getEnableTMVPFlag() && pInfo->numCand < AMVP_MAX_NUM_CANDS && (pu.lumaSize().width + pu.lumaSize().height > 12))
  {
    // Get Temporal Motion Predictor
    const int refIdxCol = refIdx;

    Position posRB = pu.Y().bottomRight().offset(-3, -3);

    const PreCalcValues& pcv = *cs.pcv;

    Position posC0;
    bool C0Avail = false;
    Position posC1 = pu.Y().center();
    Mv cColMv;

    bool boundaryCond = ((posRB.x + pcv.minCUWidth) < pcv.lumaWidth) && ((posRB.y + pcv.minCUHeight) < pcv.lumaHeight);
    const SubPic &curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
    if (curSubPic.getTreatedAsPicFlag())
    {
      boundaryCond = ((posRB.x + pcv.minCUWidth) <= curSubPic.getSubPicRight() &&
                      (posRB.y + pcv.minCUHeight) <= curSubPic.getSubPicBottom());
    }
    if (boundaryCond)
    {
      int posYInCtu = posRB.y & pcv.maxCUHeightMask;
      if (posYInCtu + 4 < pcv.maxCUHeight)
      {
        posC0 = posRB.offset(4, 4);
        C0Avail = true;
      }
    }
    if ((C0Avail && getColocatedMVP(pu, eRefPicList, posC0, cColMv, refIdxCol, false))
        || getColocatedMVP(pu, eRefPicList, posC1, cColMv, refIdxCol, false))
    {
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Mv   ccMv;
        bool posC0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
        bool posC1inCurPicSolid = cs.isClean(posC1, CHANNEL_TYPE_LUMA);
        bool posC0inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_0, refIdxCol);
        bool posC1inRefPicSolid = cs.isClean(posC1, REF_PIC_LIST_0, refIdxCol);

        bool isMVP0exist = C0Avail && getColocatedMVP(pu, eRefPicList, posC0, ccMv, refIdxCol, false);

        Position pos = isMVP0exist ? posC0 : posC1;
        pInfo->mvPos[pInfo->numCand]   = pos;
        pInfo->mvType[pInfo->numCand]  = isMVP0exist ? MVP_TMVP_C0 : MVP_TMVP_C1;
        pInfo->mvSolid[pInfo->numCand] = allCandSolidInAbove && (isMVP0exist ? (posC0inCurPicSolid && posC0inRefPicSolid) : (posC1inCurPicSolid && posC1inRefPicSolid));
        allCandSolidInAbove = allCandSolidInAbove && pInfo->mvSolid[pInfo->numCand];
      }
#endif
      cColMv.roundTransPrecInternal2Amvr(pu.cu->imv);
      pInfo->mvCand[pInfo->numCand++] = cColMv;
    }
  }

  if (pInfo->numCand < AMVP_MAX_NUM_CANDS)
  {
    const int currRefPOC = cs.slice->getRefPic(eRefPicList, refIdx)->getPOC();
    addAMVPHMVPCand(pu, eRefPicList, currRefPOC, *pInfo);
  }

  if (pInfo->numCand > AMVP_MAX_NUM_CANDS)
  {
    pInfo->numCand = AMVP_MAX_NUM_CANDS;
  }

  while (pInfo->numCand < AMVP_MAX_NUM_CANDS)
  {
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      pInfo->mvType[pInfo->numCand] = MVP_ZERO;
      allCandSolidInAbove = pInfo->mvSolid[pInfo->numCand] = true && allCandSolidInAbove;
    }
#endif
    pInfo->mvCand[pInfo->numCand] = Mv( 0, 0 );
    pInfo->numCand++;
  }

  for (Mv &mv : pInfo->mvCand)
  {
    mv.roundTransPrecInternal2Amvr(pu.cu->imv);
  }
}

bool PU::addAffineMVPCandUnscaled( const PredictionUnit &pu, const RefPicList &refPicList, const int &refIdx, const Position &pos, const MvpDir &dir, AffineAMVPInfo &affiAMVPInfo )
{
  CodingStructure &cs = *pu.cs;
  const PredictionUnit *neibPU = nullptr;
  Position neibPos;

#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif
  switch ( dir )
  {
  case MD_LEFT:
    neibPos = pos.offset( -1, 0 );
    break;
  case MD_ABOVE:
    neibPos = pos.offset( 0, -1 );
    break;
  case MD_ABOVE_RIGHT:
    neibPos = pos.offset( 1, -1 );
    break;
  case MD_BELOW_LEFT:
    neibPos = pos.offset( -1, 1 );
    break;
  case MD_ABOVE_LEFT:
    neibPos = pos.offset( -1, -1 );
    break;
  default:
    break;
  }

  neibPU = cs.getPURestricted( neibPos, pu, pu.chType );

  if (neibPU == nullptr || !CU::isInter(*neibPU->cu) || !neibPU->cu->affine || neibPU->mergeType != MRG_TYPE_DEFAULT_N)
  {
    return false;
  }

  Mv outputAffineMv[3];
#if GDR_ENABLED
  bool     outputAffineMvSolid[3];
  MvpType  outputAffineMvType[3];
  Position outputAffineMvPos[3];
#endif
  const MotionInfo& neibMi = neibPU->getMotionInfo( neibPos );

  const int        currRefPOC = cs.slice->getRefPic( refPicList, refIdx )->getPOC();
  const RefPicList refPicList2nd = (refPicList == REF_PIC_LIST_0) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

  for ( int predictorSource = 0; predictorSource < 2; predictorSource++ ) // examine the indicated reference picture list, then if not available, examine the other list.
  {
    const RefPicList eRefPicListIndex = (predictorSource == 0) ? refPicList : refPicList2nd;
    const int        neibRefIdx = neibMi.refIdx[eRefPicListIndex];

    if ( ((neibPU->interDir & (eRefPicListIndex + 1)) == 0) || pu.cu->slice->getRefPOC( eRefPicListIndex, neibRefIdx ) != currRefPOC )
    {
      continue;
    }

#if GDR_ENABLED
    // note : get MV from neihgbor of neibPu (LB, RB) and save to outputAffineMv
    if (isEncodeGdrClean)
    {
      xInheritedAffineMv(pu, neibPU, eRefPicListIndex, outputAffineMv, outputAffineMvSolid, outputAffineMvType, outputAffineMvPos);
    }
    else
    {
      xInheritedAffineMv(pu, neibPU, eRefPicListIndex, outputAffineMv);
    }
#else
    xInheritedAffineMv( pu, neibPU, eRefPicListIndex, outputAffineMv );
#endif
    outputAffineMv[0].roundAffinePrecInternal2Amvr(pu.cu->imv);
    outputAffineMv[1].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[0];
    affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[1];
#if GDR_ENABLED
    bool neighClean = true;

    if (isEncodeGdrClean)
    {
      neighClean = cs.isClean(neibPU->Y().pos(), CHANNEL_TYPE_LUMA);

      affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = neighClean && outputAffineMvSolid[0];
      affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = neighClean && outputAffineMvSolid[1];

      affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand]  = outputAffineMvType[0];
      affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand]  = outputAffineMvType[1];

      affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand]   = outputAffineMvPos[0];
      affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand]   = outputAffineMvPos[1];
    }
#endif
    if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
    {
      outputAffineMv[2].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[2];
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        neighClean = cs.isClean(neibPU->Y().pos(), CHANNEL_TYPE_LUMA);

        affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = neighClean && outputAffineMvSolid[2];
        affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand]  = outputAffineMvType[2];
        affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand]   = outputAffineMvPos[2];
      }
#endif
    }
    affiAMVPInfo.numCand++;
    return true;
  }

  return false;
}

#if GDR_ENABLED
void PU::xInheritedAffineMv(const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList eRefPicList, Mv rcMv[3], bool rcMvSolid[3], MvpType rcMvType[3], Position rcMvPos[3])
{
  const CodingStructure &cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));

  int posNeiX = puNeighbour->Y().pos().x;
  int posNeiY = puNeighbour->Y().pos().y;
  int posCurX = pu.Y().pos().x;
  int posCurY = pu.Y().pos().y;

  int neiW = puNeighbour->Y().width;
  int curW = pu.Y().width;
  int neiH = puNeighbour->Y().height;
  int curH = pu.Y().height;

  Mv mvLT, mvRT, mvLB;

  mvLT = puNeighbour->mvAffi[eRefPicList][0];
  mvRT = puNeighbour->mvAffi[eRefPicList][1];
  mvLB = puNeighbour->mvAffi[eRefPicList][2];


#if GDR_ENABLED
  bool neighClean = true;

  if (isEncodeGdrClean)
  {
    neighClean = cs.isClean(puNeighbour->Y().pos(), CHANNEL_TYPE_LUMA);

    rcMvSolid[0] = neighClean;
    rcMvSolid[1] = neighClean;
    rcMvSolid[2] = neighClean;

    rcMvType[0] = AFFINE_INHERIT;
    rcMvType[1] = AFFINE_INHERIT;
    rcMvType[2] = AFFINE_INHERIT;

    rcMvPos[0] = puNeighbour->Y().pos();
    rcMvPos[1] = puNeighbour->Y().pos();
    rcMvPos[2] = puNeighbour->Y().pos();
  }
#endif


  bool isTopCtuBoundary = false;
  if ((posNeiY + neiH) % pu.cs->sps->getCTUSize() == 0 && (posNeiY + neiH) == posCurY)
  {
    // use bottom-left and bottom-right sub-block MVs for inheritance
    const Position posRB = puNeighbour->Y().bottomRight();
    const Position posLB = puNeighbour->Y().bottomLeft();

    mvLT = puNeighbour->getMotionInfo(posLB).mv[eRefPicList];
    mvRT = puNeighbour->getMotionInfo(posRB).mv[eRefPicList];

#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      neighClean = cs.isClean(puNeighbour->Y().pos(), CHANNEL_TYPE_LUMA);

      rcMvSolid[0] = cs.isClean(posLB, CHANNEL_TYPE_LUMA);
      rcMvSolid[1] = cs.isClean(posRB, CHANNEL_TYPE_LUMA);
      rcMvSolid[2] = neighClean;

      rcMvType[0]  = AFFINE_INHERIT_LB_RB;
      rcMvType[1]  = AFFINE_INHERIT_LB_RB;
      rcMvType[2]  = AFFINE_INHERIT_LB_RB;

      rcMvPos[0]   = posLB;
      rcMvPos[1]   = posRB;
      rcMvPos[2]   = puNeighbour->Y().pos();
    }
#endif

    posNeiY += neiH;
    isTopCtuBoundary = true;
  }

  int shift = MAX_CU_DEPTH;
  int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

  dmvHorX = (mvRT - mvLT).getHor() << (shift - floorLog2(neiW));
  dmvHorY = (mvRT - mvLT).getVer() << (shift - floorLog2(neiW));
  if (puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary)
  {
    dmvVerX = (mvLB - mvLT).getHor() << (shift - floorLog2(neiH));
    dmvVerY = (mvLB - mvLT).getVer() << (shift - floorLog2(neiH));
  }
  else
  {
    dmvVerX = -dmvHorY;
    dmvVerY = dmvHorX;
  }

  int mvScaleHor = mvLT.getHor() << shift;
  int mvScaleVer = mvLT.getVer() << shift;

  // v0
  rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
  rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
  rcMv[0].roundAffine(shift);
  rcMv[0].clipToStorageBitDepth();

  // v1
  rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
  rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
  rcMv[1].roundAffine(shift);
  rcMv[1].clipToStorageBitDepth();

  // v2
  if (pu.cu->affineType == AFFINEMODEL_6PARAM)
  {
    rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
    rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
    rcMv[2].roundAffine(shift);
    rcMv[2].clipToStorageBitDepth();
  }
}
#endif

void PU::xInheritedAffineMv( const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList eRefPicList, Mv rcMv[3] )
{
  int posNeiX = puNeighbour->Y().pos().x;
  int posNeiY = puNeighbour->Y().pos().y;
  int posCurX = pu.Y().pos().x;
  int posCurY = pu.Y().pos().y;

  int neiW = puNeighbour->Y().width;
  int curW = pu.Y().width;
  int neiH = puNeighbour->Y().height;
  int curH = pu.Y().height;

  Mv mvLT, mvRT, mvLB;
  mvLT = puNeighbour->mvAffi[eRefPicList][0];
  mvRT = puNeighbour->mvAffi[eRefPicList][1];
  mvLB = puNeighbour->mvAffi[eRefPicList][2];


  bool isTopCtuBoundary = false;
  if ( (posNeiY + neiH) % pu.cs->sps->getCTUSize() == 0 && (posNeiY + neiH) == posCurY )
  {
    // use bottom-left and bottom-right sub-block MVs for inheritance
    const Position posRB = puNeighbour->Y().bottomRight();
    const Position posLB = puNeighbour->Y().bottomLeft();
    mvLT = puNeighbour->getMotionInfo( posLB ).mv[eRefPicList];
    mvRT = puNeighbour->getMotionInfo( posRB ).mv[eRefPicList];
    posNeiY += neiH;
    isTopCtuBoundary = true;
  }

  int shift = MAX_CU_DEPTH;
  int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

  dmvHorX = (mvRT - mvLT).getHor() * (1 << (shift - floorLog2(neiW)));
  dmvHorY = (mvRT - mvLT).getVer() * (1 << (shift - floorLog2(neiW)));
  if ( puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary )
  {
    dmvVerX = (mvLB - mvLT).getHor() * (1 << (shift - floorLog2(neiH)));
    dmvVerY = (mvLB - mvLT).getVer() * (1 << (shift - floorLog2(neiH)));
  }
  else
  {
    dmvVerX = -dmvHorY;
    dmvVerY = dmvHorX;
  }

  int mvScaleHor = mvLT.getHor() * (1 << shift);
  int mvScaleVer = mvLT.getVer() * (1 << shift);

  // v0
  rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
  rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
  rcMv[0].roundAffine(shift);
  rcMv[0].clipToStorageBitDepth();

  // v1
  rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
  rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
  rcMv[1].roundAffine(shift);
  rcMv[1].clipToStorageBitDepth();

  // v2
  if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
  {
    rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
    rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
    rcMv[2].roundAffine(shift);
    rcMv[2].clipToStorageBitDepth();
  }
}

#if POSE
void PU::xInheritedposeMv(const PredictionUnit &pu, const PredictionUnit *puNeighbour, RefPicList eRefPicList,
                            Mv rcMv[3], bool rcMvSolid[3], MvpType rcMvType[3], Position rcMvPos[3])
{
  const CodingStructure &cs = *pu.cs;
  const bool             isEncodeGdrClean =
    cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder
    && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA))
        || (cs.picHeader->getNumVerVirtualBoundaries() == 0));

  int posNeiX = puNeighbour->Y().pos().x;
  int posNeiY = puNeighbour->Y().pos().y;
  int posCurX = pu.Y().pos().x;
  int posCurY = pu.Y().pos().y;

  int neiW = puNeighbour->Y().width;
  int curW = pu.Y().width;
  int neiH = puNeighbour->Y().height;
  int curH = pu.Y().height;

  Mv mvLT, mvRT, mvLB;

  mvLT = puNeighbour->mvAffi[eRefPicList][0];
  mvRT = puNeighbour->mvAffi[eRefPicList][1];
  mvLB = puNeighbour->mvAffi[eRefPicList][2];

#if GDR_ENABLED
  bool neighClean = true;

  if (isEncodeGdrClean)
  {
    neighClean = cs.isClean(puNeighbour->Y().pos(), CHANNEL_TYPE_LUMA);

    rcMvSolid[0] = neighClean;
    rcMvSolid[1] = neighClean;
    rcMvSolid[2] = neighClean;

    rcMvType[0] = AFFINE_INHERIT;
    rcMvType[1] = AFFINE_INHERIT;
    rcMvType[2] = AFFINE_INHERIT;

    rcMvPos[0] = puNeighbour->Y().pos();
    rcMvPos[1] = puNeighbour->Y().pos();
    rcMvPos[2] = puNeighbour->Y().pos();
  }
#endif

  bool isTopCtuBoundary = false;
  if ((posNeiY + neiH) % pu.cs->sps->getCTUSize() == 0 && (posNeiY + neiH) == posCurY)
  {
    // use bottom-left and bottom-right sub-block MVs for inheritance
    const Position posRB = puNeighbour->Y().bottomRight();
    const Position posLB = puNeighbour->Y().bottomLeft();

    mvLT = puNeighbour->getMotionInfo(posLB).mv[eRefPicList];
    mvRT = puNeighbour->getMotionInfo(posRB).mv[eRefPicList];

#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      neighClean = cs.isClean(puNeighbour->Y().pos(), CHANNEL_TYPE_LUMA);

      rcMvSolid[0] = cs.isClean(posLB, CHANNEL_TYPE_LUMA);
      rcMvSolid[1] = cs.isClean(posRB, CHANNEL_TYPE_LUMA);
      rcMvSolid[2] = neighClean;

      rcMvType[0] = AFFINE_INHERIT_LB_RB;
      rcMvType[1] = AFFINE_INHERIT_LB_RB;
      rcMvType[2] = AFFINE_INHERIT_LB_RB;

      rcMvPos[0] = posLB;
      rcMvPos[1] = posRB;
      rcMvPos[2] = puNeighbour->Y().pos();
    }
#endif

    posNeiY += neiH;
    isTopCtuBoundary = true;
  }

  /*新增*/
  if (puNeighbour->cu->poseFlag == 1)
  {
    rcMv[0] = mvLT;
    rcMv[1] = mvRT;
    rcMv[2] = mvLB;
    rcMv[0].clipToStorageBitDepth();
    rcMv[1].clipToStorageBitDepth();
    rcMv[2].clipToStorageBitDepth();
  }
  else if (puNeighbour->cu->affine == 1)
  {
     int shift = MAX_CU_DEPTH;
     int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

     dmvHorX = (mvRT - mvLT).getHor() << (shift - floorLog2(neiW));
     dmvHorY = (mvRT - mvLT).getVer() << (shift - floorLog2(neiW));
     if (puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary)
    {
      dmvVerX = (mvLB - mvLT).getHor() << (shift - floorLog2(neiH));
      dmvVerY = (mvLB - mvLT).getVer() << (shift - floorLog2(neiH));
    }
     else
    {
      dmvVerX = -dmvHorY;
      dmvVerY = dmvHorX;
    }

     int mvScaleHor = mvLT.getHor() << shift;
     int mvScaleVer = mvLT.getVer() << shift;

    // v0
     rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
     rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
     rcMv[0].roundAffine(shift);
     rcMv[0].clipToStorageBitDepth();

    // v1
     rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
     rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
     rcMv[1].roundAffine(shift);
     rcMv[1].clipToStorageBitDepth();

    // v2
     if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
      rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
      rcMv[2].roundAffine(shift);
      rcMv[2].clipToStorageBitDepth();
    }
  }

  //int shift = MAX_CU_DEPTH;
  //int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

  //dmvHorX = (mvRT - mvLT).getHor() << (shift - floorLog2(neiW));
  //dmvHorY = (mvRT - mvLT).getVer() << (shift - floorLog2(neiW));
  //if (puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary)
  //{
  //  dmvVerX = (mvLB - mvLT).getHor() << (shift - floorLog2(neiH));
  //  dmvVerY = (mvLB - mvLT).getVer() << (shift - floorLog2(neiH));
  //}
  //else
  //{
  //  dmvVerX = -dmvHorY;
  //  dmvVerY = dmvHorX;
  //}

  //int mvScaleHor = mvLT.getHor() << shift;
  //int mvScaleVer = mvLT.getVer() << shift;

  //// v0
  //rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
  //rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
  //rcMv[0].roundAffine(shift);
  //rcMv[0].clipToStorageBitDepth();

  //// v1
  //rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
  //rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
  //rcMv[1].roundAffine(shift);
  //rcMv[1].clipToStorageBitDepth();

  //// v2
  //if (pu.cu->affineType == AFFINEMODEL_6PARAM)
  //{
  //  rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
  //  rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
  //  rcMv[2].roundAffine(shift);
  //  rcMv[2].clipToStorageBitDepth();
  //}
}

void PU::xInheritedposeMv( const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList eRefPicList, Mv rcMv[3] )
{
  int posNeiX = puNeighbour->Y().pos().x;
  int posNeiY = puNeighbour->Y().pos().y;
  int posCurX = pu.Y().pos().x;
  int posCurY = pu.Y().pos().y;

  int neiW = puNeighbour->Y().width;
  int curW = pu.Y().width;
  int neiH = puNeighbour->Y().height;
  int curH = pu.Y().height;

  Mv mvLT, mvRT, mvLB;
  mvLT = puNeighbour->mvAffi[eRefPicList][0];
  mvRT = puNeighbour->mvAffi[eRefPicList][1];
  mvLB = puNeighbour->mvAffi[eRefPicList][2];


  bool isTopCtuBoundary = false;
  if ( (posNeiY + neiH) % pu.cs->sps->getCTUSize() == 0 && (posNeiY + neiH) == posCurY )//ljy:一定不满足 左块和左下块的y+h一定大于当前的y
  {
    // use bottom-left and bottom-right sub-block MVs for inheritance
    const Position posRB = puNeighbour->Y().bottomRight();
    const Position posLB = puNeighbour->Y().bottomLeft();
    mvLT = puNeighbour->getMotionInfo( posLB ).mv[eRefPicList];
    mvRT = puNeighbour->getMotionInfo( posRB ).mv[eRefPicList];
    posNeiY += neiH;
    isTopCtuBoundary = true;
  }

  /*新增*/
  if (puNeighbour->cu->poseFlag)
  {
    {
#if CTRL_COMB
      EPoseModel neiPoseType = (EPoseModel)puNeighbour->cu->poseType;
#endif
      const Picture *      refPic  = pu.cu->slice->getRefPic(eRefPicList, puNeighbour->refIdx[0])->unscaledPic;
      const Picture *const prefPic = refPic;
      /*计算当前两个点和前帧的两个匹配点*/
    //double f1 = -1.1106, f2 = 1.1106; //ljy
      double f1 = -1.732, f2 = 1.732;
      double u1, v1, u2, v2, u3, v3, u4, v4;
#if CTRL_COMB
      if (neiPoseType == POSE_RT_LB)
      {
        u1 = double(puNeighbour->Y().topRight().x);//ljy
        v1 = double(puNeighbour->Y().topRight().y);   // puNeighbour的右上控制点

        u3 = double(puNeighbour->Y().bottomLeft().x);
        v3 = double(puNeighbour->Y().bottomLeft().y);   //左下控制点

        u2 = double(u1 + double(mvRT.getHor()) / 16);
        v2 = double(v1 + double(mvRT.getVer()) / 16);
        u4 = double(u3 + double(mvLB.getHor()) / 16);
        v4 = double(v3 + double(mvLB.getVer()) / 16);
      }
      else if (neiPoseType == POSE_LT_LB)
      {
        u1 = double(puNeighbour->Y().x);//ljy
        v1 = double(puNeighbour->Y().y);   // puNeighbour的左上控制点

        u3 = double(puNeighbour->Y().bottomLeft().x);
        v3 = double(puNeighbour->Y().bottomLeft().y);   //左下控制点

        u2 = double(u1 + double(mvLT.getHor()) / 16);
        v2 = double(v1 + double(mvLT.getVer()) / 16);
        u4 = double(u3 + double(mvLB.getHor()) / 16);
        v4 = double(v3 + double(mvLB.getVer()) / 16);
      }
      else//POSE_LT_RT
      {
        u1 = double(puNeighbour->Y().x);//ljy
        v1 = double(puNeighbour->Y().y);   // puNeighbour的左上控制点

        u3 = double(puNeighbour->Y().topRight().x);
        v3 = double(puNeighbour->Y().topRight().y);   //右上控制点

        u2 = double(u1 + double(mvLT.getHor()) / 16);
        v2 = double(v1 + double(mvLT.getVer()) / 16);
        u4 = double(u3 + double(mvRT.getHor()) / 16);
        v4 = double(v3 + double(mvRT.getVer()) / 16);
      }
#else
      u1 = double(puNeighbour->Y().topRight().x);//ljy
      v1 = double(puNeighbour->Y().topRight().y);   // puNeighbour的右上控制点  右上控制点

      u3 = double(puNeighbour->Y().bottomLeft().x);
      v3 = double(puNeighbour->Y().bottomLeft().y);   //左下控制点

      u2 = double(u1 + double(mvRT.getHor()) / 16);
      v2 = double(v1 + double(mvRT.getVer()) / 16);
      u4 = double(u3 + double(mvLB.getHor()) / 16);
      v4 = double(v3 + double(mvLB.getVer()) / 16);
#endif

      //归一化
      u1 = (u1 / pu.cs->picture->lwidth()) * 2 - 1;
      v1 = (v1 / pu.cs->picture->lwidth()) * 2 - 1;
      u3 = (u3 / pu.cs->picture->lwidth()) * 2 - 1;
      v3 = (v3 / pu.cs->picture->lwidth()) * 2 - 1;
      u2 = (u2 / pu.cs->picture->lwidth()) * 2 - 1;
      v2 = (v2 / pu.cs->picture->lwidth()) * 2 - 1;
      u4 = (u4 / pu.cs->picture->lwidth()) * 2 - 1;
      v4 = (v4 / pu.cs->picture->lwidth()) * 2 - 1;
      /*根据匹配点计算中间过程参数*/
      double E1, H1, I1, M1, E3, H3, I3, M3;
      double AA, P1A, P2A, P3A, P4A, Q1A, Q2A, Q3A, Q4A;   //当前pic存的参数；
      double AB, P1B, P2B, P3B, P4B, Q1B, Q2B, Q3B, Q4B;   //前一帧pic存的参数
      //当前帧的参数
      AA  = pu.cs->picture->AA;
      P1A = pu.cs->picture->P1A;
      P2A = pu.cs->picture->P2A;
      P3A = pu.cs->picture->P3A;
      P4A = pu.cs->picture->P4A;
      Q1A = pu.cs->picture->Q1A;
      Q2A = pu.cs->picture->Q2A;
      Q3A = pu.cs->picture->Q3A;
      Q4A = pu.cs->picture->Q4A;
      //前一帧的参数
      AB  = prefPic->AA;
      P1B = prefPic->P1A;
      P2B = prefPic->P2A;
      P3B = prefPic->P3A;
      P4B = prefPic->P4A;
      Q1B = prefPic->Q1A;
      Q2B = prefPic->Q2A;
      Q3B = prefPic->Q3A;
      Q4B = prefPic->Q4A;

      // CY 这是按照Unity中取-f1+f2+1的公式形式
      E1 = ((P1A / P1B) * u1 - f1 * v1 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点1、2的参数theta_u的参数
      H1 = (f1 * v2 * P4B / (f2 * P1B)) - f1 * P3B / P1B;                      //配对点1、2的常数H

      I1 = (Q4A / Q4B) * v1 - f2 * u1 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;   //配对点1、2的参数theta_v的参数   same v1总是等于v3的
      M1 = (Q3B * f2 * u2 / (f1 * Q4B)) + f2 * Q1B / Q4B;   //配对点1、2的常数M

      E3 = ((P1A / P1B) * u3 - f1 * v3 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点3、4的参数theta_u的参数
      H3 = (f1 * v4 * P4B / (f2 * P1B)) - f1 * P3B / P1B;                      //配对点3、4的常数H

      I3 = (Q4A / Q4B) * v3 - f2 * u3 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;   //配对点3、4的参数theta_v的参数   same
      M3 = (Q3B * f2 * u4 / (f1 * Q4B)) + f2 * Q1B / Q4B;                    //配对点3、4的常数M

      // CY 这是按照The Lab中取-f1+f2-1的公式形式
      //E1 = ((P1A / P1B) * u1 - f1 * v1 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点1、2的参数theta_u的参数
      //H1 = (f1 * v2 * P4B / (f2 * P1B)) + f1 * P3B / P1B;                      //配对点1、2的常数H

      //I1 = (Q4A / Q4B) * v1 - f2 * u1 * Q3A / (f1 * Q4B)
      //     + f2 * Q1A / Q4B;   //配对点1、2的参数theta_v的参数   same v1总是等于v3的
      //M1 = (Q3B * f2 * u2 / (f1 * Q4B)) - f2 * Q1B / Q4B;   //配对点1、2的常数M

      //E3 = ((P1A / P1B) * u3 - f1 * v3 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点3、4的参数theta_u的参数
      //H3 = (f1 * v4 * P4B / (f2 * P1B)) + f1 * P3B / P1B;                      //配对点3、4的常数H

      //I3 = (Q4A / Q4B) * v3 - f2 * u3 * Q3A / (f1 * Q4B) + f2 * Q1A / Q4B;   //配对点3、4的参数theta_v的参数   same
      //M3 = (Q3B * f2 * u4 / (f1 * Q4B)) - f2 * Q1B / Q4B;                    //配对点3、4的常数M

#if THETA_UV
      double theta_u, theta_v, S, J, theta;   //最终的参数
      theta_u = -(E3 * v2 - E3 * v4 - I3 * u2 + I3 * u4 + H1 * I3 - H3 * I3 - E3 * M1 + E3 * M3) / (E1 * I3 - E3 * I1);
      S = (E3 * H1 * I1 - E1 * H3 * I3 - E1 * E3 * M1 + E1 * E3 * M3 + E1 * E3 * v2 - E1 * E3 * v4 - E3 * I1 * u2 + E1 * I3 * u4) / (E1 * I3 - E3 * I1);
      theta_v = -(E1 * v2 - E1 * v4 - I1 * u2 + I1 * u4 + H1 * I1 - H3 * I1 - E1 * M1 + E1 * M3) / (E1 * I3 - E3 * I1);
      J = (H1 * I1 * I3 - H3 * I1 * I3 - E1 * I3 * M1 + E3 * I1 * M3 + E1 * I3 * v2 - E3 * I1 * v4 - I1 * I3 * u2 + I1 * I3 * u4) / (E1 * I3 - E3 * I1);
#else
      double theta_u, S, theta_v, J;   //最终的参数
      theta_u = (u2 - u4 - H1 + H3) / (E1 - E3);
      S       = -(u2 * E3 - u4 * E1 + E1 * H3 - E3 * H1) / (E1 - E3);
      theta_v = (v2 - v4 - M1 + M3) / (I1 - I3);
      J       = -(v2 * I3 - v4 * I1 + I1 * M3 - I3 * M1) / (I1 - I3);
#endif

      double u5, v5, u6, v6, E5, I5, A, u61, v61;
      double detalx = 0, detaly = 0;
      int    detalx_int = 0, detaly_int = 0;

      for (int loop = 0; loop < 3; loop++)   //遍历左上右上左下   通过邻居cu推出来的模型参数用于推导cur的控制点MV
      {
        if (loop == 0)   // cur左上角位置
        {
          u5 = double(pu.Y().topLeft().x) / pu.cs->picture->lwidth() * 2 - 1;
          v5 = double(pu.Y().topLeft().y) / pu.cs->picture->lwidth() * 2 - 1;
        }
        else if (loop == 1)   // cur右上角
        {
          u5 = double(pu.Y().topRight().x) / pu.cs->picture->lwidth() * 2 - 1;//ljy
          v5 = double(pu.Y().topRight().y) / pu.cs->picture->lwidth() * 2 - 1;
        }
        else if (loop == 2)
        {
          u5 = double(pu.Y().bottomLeft().x) / pu.cs->picture->lwidth() * 2 - 1;
          v5 = double(pu.Y().bottomLeft().y) / pu.cs->picture->lwidth() * 2 - 1;
        }

        // CY 这是按照Unity中取-f1+f2+1的公式形式
        E5  = ((P1A / P1B) * u5 - f1 * v5 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点1的参数A的参数
        I5  = (Q4A / Q4B) * v5 - f2 * u5 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;     //配对点1的参数A的参数

        // CY 这是按照The Lab中取-f1+f2-1的公式形式
        //E5  = ((P1A / P1B) * u5 - f1 * v5 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点1的参数A的参数
        //I5  = (Q4A / Q4B) * v5 - f2 * u5 * Q3A / (f1 * Q4B) + f2 * Q1A / Q4B;     //配对点1的参数A的参数

        A   = 1 / (1 - (P4B * Q3B) / (P1B * Q4B));

#if THETA_UV
        double q = sqrt((u5 - u1) * (u5 - u1) + (v5 - v1) * (v5 - v1)) / sqrt((u3 - u1) * (u3 - u1) + (v3 - v1) * (v3 - v1));
        theta = theta_u + q * (theta_v - theta_u);
        // CY 这是按照Unity中取-f1+f2+1的公式形式
        u61 = A
          * (theta * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S - f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
            + (f1 * P4B * Q1B) / (P1B * Q4B));
        v61 = A
          * (theta * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) + f2 * Q1B / Q4B
            - (Q3B * f2 * P3B) / (Q4B * P1B));
        // CY 这是按照The Lab中取-f1+f2-1的公式形式
      /*  u61 = A
              * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S + f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
                 - (f1 * P4B * Q1B) / (P1B * Q4B));
        v61 = A
              * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) - f2 * Q1B / Q4B
                 + (Q3B * f2 * P3B) / (Q4B * P1B));*/
#else
        // CY 这是按照Unity中取-f1+f2+1的公式形式
        u61 = A
              * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S - f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
                 + (f1 * P4B * Q1B) / (P1B * Q4B));
        v61 = A
              * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) + f2 * Q1B / Q4B
                 - (Q3B * f2 * P3B) / (Q4B * P1B));

        // CY 这是按照The Lab中取-f1+f2-1的公式形式
        //u61 = A
        //      * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S + f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
        //         - (f1 * P4B * Q1B) / (P1B * Q4B));
        //v61 = A
        //      * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) - f2 * Q1B / Q4B
        //         + (Q3B * f2 * P3B) / (Q4B * P1B));
#endif

        /*归一化坐标转化成像素坐标*/

        u5 = (u5 + 1) * pu.cs->picture->lwidth()/2;
        v5 = (v5 + 1) * pu.cs->picture->lwidth()/2;
        if (isnan(u61))
        {
          u6     = 0;
          detalx = 0;
        }
        else
        {
          u6     = (u61 + 1) * pu.cs->picture->lwidth()/2;   //归一化转像素坐标
          detalx = (u6 - u5) * 16;    //转成1/16精度的MV存起来
        }
        if (isnan(v61))
        {
          v6     = 0;
          detaly = 0;
        }
        else
        {
          v6     = (v61 + 1) * pu.cs->picture->lwidth()/2;
          detaly = (v6 - v5) * 16;
        }

#if INT2ROUND
       detalx_int = int  (round(detalx));
       detaly_int = int  (round(detaly));
#else
        detalx_int = int(detalx);
        detaly_int = int(detaly);
#endif
        Mv cursampleMv(detalx_int, detaly_int);
        cursampleMv.clipToStorageBitDepth();

        rcMv[loop] = cursampleMv;
      }
    }
  }
  else if (puNeighbour->cu->affine)
  {
     int shift = MAX_CU_DEPTH;
     int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

     dmvHorX = (mvRT - mvLT).getHor() * (1 << (shift - floorLog2(neiW)));
     dmvHorY = (mvRT - mvLT).getVer() * (1 << (shift - floorLog2(neiW)));
     if ( puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary )
    {
      dmvVerX = (mvLB - mvLT).getHor() * (1 << (shift - floorLog2(neiH)));
      dmvVerY = (mvLB - mvLT).getVer() * (1 << (shift - floorLog2(neiH)));
    }
     else
    {
      dmvVerX = -dmvHorY;
      dmvVerY = dmvHorX;
    }

     int mvScaleHor = mvLT.getHor() * (1 << shift);
     int mvScaleVer = mvLT.getVer() * (1 << shift);

    // v0
     rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
     rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
     rcMv[0].roundAffine(shift);
     rcMv[0].clipToStorageBitDepth();

    // v1
     rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
     rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
     rcMv[1].roundAffine(shift);
     rcMv[1].clipToStorageBitDepth();

    // v2
#if !INHERITED
     if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
#endif
    {
      rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
      rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
      rcMv[2].roundAffine(shift);
      rcMv[2].clipToStorageBitDepth();
    }
  }

  //int shift = MAX_CU_DEPTH;
  //int dmvHorX, dmvHorY, dmvVerX, dmvVerY;

  //dmvHorX = (mvRT - mvLT).getHor() * (1 << (shift - floorLog2(neiW)));
  //dmvHorY = (mvRT - mvLT).getVer() * (1 << (shift - floorLog2(neiW)));
  //if ( puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary )
  //{
  //  dmvVerX = (mvLB - mvLT).getHor() * (1 << (shift - floorLog2(neiH)));
  //  dmvVerY = (mvLB - mvLT).getVer() * (1 << (shift - floorLog2(neiH)));
  //}
  //else
  //{
  //  dmvVerX = -dmvHorY;
  //  dmvVerY = dmvHorX;
  //}

  //int mvScaleHor = mvLT.getHor() * (1 << shift);
  //int mvScaleVer = mvLT.getVer() * (1 << shift);

  //// v0
  //rcMv[0].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY - posNeiY);
  //rcMv[0].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY - posNeiY);
  //rcMv[0].roundAffine(shift);
  //rcMv[0].clipToStorageBitDepth();

  //// v1
  //rcMv[1].hor = mvScaleHor + dmvHorX * (posCurX + curW - posNeiX) + dmvVerX * (posCurY - posNeiY);
  //rcMv[1].ver = mvScaleVer + dmvHorY * (posCurX + curW - posNeiX) + dmvVerY * (posCurY - posNeiY);
  //rcMv[1].roundAffine(shift);
  //rcMv[1].clipToStorageBitDepth();

  //// v2
  //if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
  //{
  //  rcMv[2].hor = mvScaleHor + dmvHorX * (posCurX - posNeiX) + dmvVerX * (posCurY + curH - posNeiY);
  //  rcMv[2].ver = mvScaleVer + dmvHorY * (posCurX - posNeiX) + dmvVerY * (posCurY + curH - posNeiY);
  //  rcMv[2].roundAffine(shift);
  //  rcMv[2].clipToStorageBitDepth();
  //}
}
#endif

void PU::fillAffineMvpCand(PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AffineAMVPInfo &affiAMVPInfo)
{
  affiAMVPInfo.numCand = 0;

  if (refIdx < 0)
  {
    return;
  }

#if GDR_ENABLED
  const CodingStructure &cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool &allCandSolidInAbove = affiAMVPInfo.allCandSolidInAbove;

  if (isEncodeGdrClean)
  {
    allCandSolidInAbove = true;

    affiAMVPInfo.allCandSolidInAbove = true;
    for (int i = 0; i < AMVP_MAX_NUM_CANDS_MEM; i++)
    {
      affiAMVPInfo.mvSolidLT[i] = true;
      affiAMVPInfo.mvSolidRT[i] = true;
      affiAMVPInfo.mvSolidLB[i] = true;
    }
  }
#endif
  // insert inherited affine candidates
  Mv outputAffineMv[3];
#if GDR_ENABLED
  bool     outputAffineMvSolid[3];
  MvpType  outputAffineMvType[3];
  Position outputAffineMvPos[3];
#endif
  Position posLT = pu.Y().topLeft();
  Position posRT = pu.Y().topRight();
  Position posLB = pu.Y().bottomLeft();

  // check left neighbor
  if ( !addAffineMVPCandUnscaled( pu, eRefPicList, refIdx, posLB, MD_BELOW_LEFT, affiAMVPInfo ) )
  {
    addAffineMVPCandUnscaled( pu, eRefPicList, refIdx, posLB, MD_LEFT, affiAMVPInfo );
  }

  // check above neighbor
  if ( !addAffineMVPCandUnscaled( pu, eRefPicList, refIdx, posRT, MD_ABOVE_RIGHT, affiAMVPInfo ) )
  {
    if ( !addAffineMVPCandUnscaled( pu, eRefPicList, refIdx, posRT, MD_ABOVE, affiAMVPInfo ) )
    {
      addAffineMVPCandUnscaled( pu, eRefPicList, refIdx, posLT, MD_ABOVE_LEFT, affiAMVPInfo );
    }
  }

  if ( affiAMVPInfo.numCand >= AMVP_MAX_NUM_CANDS )
  {
    for (int i = 0; i < affiAMVPInfo.numCand; i++)
    {
      affiAMVPInfo.mvCandLT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandRT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandLB[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    }
    return;
  }

  // insert constructed affine candidates
  int cornerMVPattern = 0;

  //-------------------  V0 (START) -------------------//
  AMVPInfo amvpInfo0;
  amvpInfo0.numCand = 0;

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    amvpInfo0.allCandSolidInAbove = true;
    for (int i = 0; i < AMVP_MAX_NUM_CANDS_MEM; i++)
    {
      amvpInfo0.mvSolid[i] = true;
      amvpInfo0.mvValid[i] = true;
    }
  }
#endif

  // A->C: Above Left, Above, Left
  addMVPCandUnscaled( pu, eRefPicList, refIdx, posLT, MD_ABOVE_LEFT, amvpInfo0 );
  if ( amvpInfo0.numCand < 1 )
  {
    addMVPCandUnscaled( pu, eRefPicList, refIdx, posLT, MD_ABOVE, amvpInfo0 );
  }
  if ( amvpInfo0.numCand < 1 )
  {
    addMVPCandUnscaled( pu, eRefPicList, refIdx, posLT, MD_LEFT, amvpInfo0 );
  }
  cornerMVPattern = cornerMVPattern | amvpInfo0.numCand;

  //-------------------  V1 (START) -------------------//
  AMVPInfo amvpInfo1;
  amvpInfo1.numCand = 0;

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    amvpInfo1.allCandSolidInAbove = true;
    for (int i = 0; i < AMVP_MAX_NUM_CANDS_MEM; i++)
    {
      amvpInfo1.mvSolid[i] = true;
      amvpInfo1.mvValid[i] = true;
    }
  }
#endif

  // D->E: Above, Above Right
  addMVPCandUnscaled( pu, eRefPicList, refIdx, posRT, MD_ABOVE, amvpInfo1 );
  if ( amvpInfo1.numCand < 1 )
  {
    addMVPCandUnscaled( pu, eRefPicList, refIdx, posRT, MD_ABOVE_RIGHT, amvpInfo1 );
  }
  cornerMVPattern = cornerMVPattern | (amvpInfo1.numCand << 1);

  //-------------------  V2 (START) -------------------//
  AMVPInfo amvpInfo2;
  amvpInfo2.numCand = 0;

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    amvpInfo2.allCandSolidInAbove = true;
    for (int i = 0; i < AMVP_MAX_NUM_CANDS_MEM; i++)
    {
      amvpInfo2.mvSolid[i] = true;
      amvpInfo2.mvValid[i] = true;
    }
  }
#endif

  // F->G: Left, Below Left
  addMVPCandUnscaled( pu, eRefPicList, refIdx, posLB, MD_LEFT, amvpInfo2 );
  if ( amvpInfo2.numCand < 1 )
  {
    addMVPCandUnscaled( pu, eRefPicList, refIdx, posLB, MD_BELOW_LEFT, amvpInfo2 );
  }
  cornerMVPattern = cornerMVPattern | (amvpInfo2.numCand << 2);

  outputAffineMv[0] = amvpInfo0.mvCand[0];
  outputAffineMv[1] = amvpInfo1.mvCand[0];
  outputAffineMv[2] = amvpInfo2.mvCand[0];

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    outputAffineMvSolid[0] = amvpInfo0.mvSolid[0] && allCandSolidInAbove;
    outputAffineMvSolid[1] = amvpInfo1.mvSolid[0] && allCandSolidInAbove;
    outputAffineMvSolid[2] = amvpInfo2.mvSolid[0] && allCandSolidInAbove;

    outputAffineMvPos[0] = amvpInfo0.mvPos[0];
    outputAffineMvPos[1] = amvpInfo1.mvPos[0];
    outputAffineMvPos[2] = amvpInfo2.mvPos[0];

    outputAffineMvType[0] = amvpInfo0.mvType[0];
    outputAffineMvType[1] = amvpInfo1.mvType[0];
    outputAffineMvType[2] = amvpInfo2.mvType[0];

    allCandSolidInAbove = allCandSolidInAbove && outputAffineMvSolid[0] && outputAffineMvSolid[1] && outputAffineMvSolid[2];
  }
#endif
  outputAffineMv[0].roundAffinePrecInternal2Amvr(pu.cu->imv);
  outputAffineMv[1].roundAffinePrecInternal2Amvr(pu.cu->imv);
  outputAffineMv[2].roundAffinePrecInternal2Amvr(pu.cu->imv);

  if ( cornerMVPattern == 7 || (cornerMVPattern == 3 && pu.cu->affineType == AFFINEMODEL_4PARAM) )
  {
    affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[0];
    affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[1];
    affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[2];
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = outputAffineMvSolid[0] && allCandSolidInAbove;
      affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = outputAffineMvSolid[1] && allCandSolidInAbove;
      affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = outputAffineMvSolid[2] && allCandSolidInAbove;

      affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand] = outputAffineMvPos[0];
      affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand] = outputAffineMvPos[1];
      affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand] = outputAffineMvPos[2];

      affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand] = outputAffineMvType[0];
      affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand] = outputAffineMvType[1];
      affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand] = outputAffineMvType[2];

      allCandSolidInAbove = allCandSolidInAbove && outputAffineMvSolid[0] && outputAffineMvSolid[1] && outputAffineMvSolid[2];
    }
#endif
    affiAMVPInfo.numCand++;
  }

  if ( affiAMVPInfo.numCand < 2 )
  {
    // check corner MVs
    for ( int i = 2; i >= 0 && affiAMVPInfo.numCand < AMVP_MAX_NUM_CANDS; i-- )
    {
      if ( cornerMVPattern & (1 << i) ) // MV i exist
      {
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[i];
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[i];
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[i];
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = outputAffineMvSolid[i] && allCandSolidInAbove;
          affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = outputAffineMvSolid[i] && allCandSolidInAbove;
          affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = outputAffineMvSolid[i] && allCandSolidInAbove;

          affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand] = outputAffineMvPos[i];
          affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand] = outputAffineMvPos[i];
          affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand] = outputAffineMvPos[i];

          affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand] = outputAffineMvType[i];
          affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand] = outputAffineMvType[i];
          affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand] = outputAffineMvType[i];

          allCandSolidInAbove = allCandSolidInAbove && outputAffineMvSolid[i];
        }
#endif
        affiAMVPInfo.numCand++;
      }
    }

    // Get Temporal Motion Predictor
    if ( affiAMVPInfo.numCand < 2 && pu.cs->picHeader->getEnableTMVPFlag() )
    {
      const int refIdxCol = refIdx;

      Position posRB = pu.Y().bottomRight().offset( -3, -3 );

      const PreCalcValues& pcv = *pu.cs->pcv;

      Position posC0;
      bool C0Avail = false;
      Position posC1 = pu.Y().center();
      Mv cColMv;
      bool boundaryCond = ((posRB.x + pcv.minCUWidth) < pcv.lumaWidth) && ((posRB.y + pcv.minCUHeight) < pcv.lumaHeight);
      const SubPic &curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
      if (curSubPic.getTreatedAsPicFlag())
      {
        boundaryCond = ((posRB.x + pcv.minCUWidth) <= curSubPic.getSubPicRight() &&
          (posRB.y + pcv.minCUHeight) <= curSubPic.getSubPicBottom());
      }
      if (boundaryCond)
      {
        int posYInCtu = posRB.y & pcv.maxCUHeightMask;
        if (posYInCtu + 4 < pcv.maxCUHeight)
        {
          posC0 = posRB.offset(4, 4);
          C0Avail = true;
        }
      }
      if ( ( C0Avail && getColocatedMVP( pu, eRefPicList, posC0, cColMv, refIdxCol, false ) ) || getColocatedMVP( pu, eRefPicList, posC1, cColMv, refIdxCol, false ) )
      {
        cColMv.roundAffinePrecInternal2Amvr(pu.cu->imv);
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = cColMv;
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = cColMv;
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = cColMv;
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Mv ccMv;

          bool posC0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
          bool posC1inCurPicSolid = cs.isClean(posC1, CHANNEL_TYPE_LUMA);
          bool posC0inRefPicSolid = cs.isClean(posC0, eRefPicList, refIdxCol);
          bool posC1inRefPicSolid = cs.isClean(posC1, eRefPicList, refIdxCol);

          bool isMVP0exist = C0Avail && getColocatedMVP(pu, eRefPicList, posC0, ccMv, refIdxCol, false);

          if (isMVP0exist)
          {
            affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = posC0inCurPicSolid && posC0inRefPicSolid && allCandSolidInAbove;
            affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = posC0inCurPicSolid && posC0inRefPicSolid && allCandSolidInAbove;
            affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = posC0inCurPicSolid && posC0inRefPicSolid && allCandSolidInAbove;

            affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand] = posC0;
            affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand] = posC0;
            affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand] = posC0;

            affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand] = MVP_TMVP_C0;
            affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand] = MVP_TMVP_C0;
            affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand] = MVP_TMVP_C0;

            allCandSolidInAbove = allCandSolidInAbove && affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand];
          }
          else
          {
            affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = posC1inCurPicSolid && posC1inRefPicSolid && allCandSolidInAbove;
            affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = posC1inCurPicSolid && posC1inRefPicSolid && allCandSolidInAbove;
            affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = posC1inCurPicSolid && posC1inRefPicSolid && allCandSolidInAbove;

            affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand] = posC1;
            affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand] = posC1;
            affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand] = posC1;

            affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand] = MVP_TMVP_C1;
            affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand] = MVP_TMVP_C1;
            affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand] = MVP_TMVP_C1;

            allCandSolidInAbove = allCandSolidInAbove && affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand];
          }
        }
#endif
        affiAMVPInfo.numCand++;
      }
    }

    if ( affiAMVPInfo.numCand < 2 )
    {
      // add zero MV
      for ( int i = affiAMVPInfo.numCand; i < AMVP_MAX_NUM_CANDS; i++ )
      {
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand].setZero();
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand].setZero();
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand].setZero();
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] = true && allCandSolidInAbove;
          affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] = true && allCandSolidInAbove;
          affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand] = true && allCandSolidInAbove;

          affiAMVPInfo.mvPosLT[affiAMVPInfo.numCand] = Position(0, 0);
          affiAMVPInfo.mvPosRT[affiAMVPInfo.numCand] = Position(0, 0);
          affiAMVPInfo.mvPosLB[affiAMVPInfo.numCand] = Position(0, 0);

          affiAMVPInfo.mvTypeLT[affiAMVPInfo.numCand] = MVP_ZERO;
          affiAMVPInfo.mvTypeRT[affiAMVPInfo.numCand] = MVP_ZERO;
          affiAMVPInfo.mvTypeLB[affiAMVPInfo.numCand] = MVP_ZERO;

          allCandSolidInAbove = allCandSolidInAbove && affiAMVPInfo.mvSolidLT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidRT[affiAMVPInfo.numCand] && affiAMVPInfo.mvSolidLB[affiAMVPInfo.numCand];
        }
#endif
        affiAMVPInfo.numCand++;
      }
    }
  }

  for (int i = 0; i < affiAMVPInfo.numCand; i++)
  {
    affiAMVPInfo.mvCandLT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandRT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandLB[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
  }
}

bool PU::addMVPCandUnscaled(const PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx,
                            const Position &pos, const MvpDir &eDir, AMVPInfo &info)
{
  CodingStructure &cs = *pu.cs;

  const PredictionUnit *neibPU = nullptr;
  Position              neibPos;

#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool &allCandSolidInAbove = info.allCandSolidInAbove;
#endif

  switch (eDir)
  {
  case MD_LEFT:
    neibPos = pos.offset( -1,  0 );
    break;
  case MD_ABOVE:
    neibPos = pos.offset(  0, -1 );
    break;
  case MD_ABOVE_RIGHT:
    neibPos = pos.offset(  1, -1 );
    break;
  case MD_BELOW_LEFT:
    neibPos = pos.offset( -1,  1 );
    break;
  case MD_ABOVE_LEFT:
    neibPos = pos.offset( -1, -1 );
    break;
  default:
    break;
  }

  neibPU = cs.getPURestricted( neibPos, pu, pu.chType );

  if (neibPU == nullptr || !CU::isInter(*neibPU->cu))
  {
    return false;
  }

  const MotionInfo& neibMi        = neibPU->getMotionInfo( neibPos );

  const int        currRefPOC     = cs.slice->getRefPic(eRefPicList, refIdx)->getPOC();
  const RefPicList eRefPicList2nd = ( eRefPicList == REF_PIC_LIST_0 ) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

  for( int predictorSource = 0; predictorSource < 2; predictorSource++ ) // examine the indicated reference picture list, then if not available, examine the other list.
  {
    const RefPicList eRefPicListIndex = ( predictorSource == 0 ) ? eRefPicList : eRefPicList2nd;
    const int        neibRefIdx       = neibMi.refIdx[eRefPicListIndex];

    if( neibRefIdx >= 0 && currRefPOC == cs.slice->getRefPOC( eRefPicListIndex, neibRefIdx ) )
    {
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        bool isSolid = cs.isClean(neibPos, CHANNEL_TYPE_LUMA);

        info.mvSolid[info.numCand] = isSolid && allCandSolidInAbove;
        info.mvType[info.numCand] = (MvpType)eDir;
        info.mvPos[info.numCand] = neibPos;

        allCandSolidInAbove = isSolid && allCandSolidInAbove;
      }
#endif
      info.mvCand[info.numCand++] = neibMi.mv[eRefPicListIndex];
      return true;
    }
  }

  return false;
}

void PU::addAMVPHMVPCand(const PredictionUnit &pu, const RefPicList eRefPicList, const int currRefPOC, AMVPInfo &info)
{
  const Slice &slice = *(*pu.cs).slice;

  MotionInfo neibMi;
  auto &lut = CU::isIBC(*pu.cu) ? pu.cs->motionLut.lutIbc : pu.cs->motionLut.lut;
  int              numAvailCandInLut = (int) lut.size();
  int              numAllowedCand    = std::min(MAX_NUM_HMVP_AVMPCANDS, numAvailCandInLut);
  const RefPicList eRefPicList2nd = (eRefPicList == REF_PIC_LIST_0) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

#if GDR_ENABLED
  CodingStructure &cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool &allCandSolidInAbove = info.allCandSolidInAbove;
#endif

#if GDR_ENABLED
  bool vbOnCtuBoundary = true;
  if (isEncodeGdrClean)
  {
    vbOnCtuBoundary = (pu.cs->picHeader->getNumVerVirtualBoundaries() == 0) || (pu.cs->picHeader->getVirtualBoundariesPosX(0) % pu.cs->sps->getMaxCUWidth() == 0);
    allCandSolidInAbove = allCandSolidInAbove && vbOnCtuBoundary;
  }
#endif

  for (int mrgIdx = 1; mrgIdx <= numAllowedCand; mrgIdx++)
  {
    if (info.numCand >= AMVP_MAX_NUM_CANDS)
    {
      return;
    }
    neibMi = lut[mrgIdx - 1];

    for (int predictorSource = 0; predictorSource < 2; predictorSource++)
    {
      const RefPicList eRefPicListIndex = (predictorSource == 0) ? eRefPicList : eRefPicList2nd;
      const int        neibRefIdx = neibMi.refIdx[eRefPicListIndex];

      if (neibRefIdx >= 0 && (CU::isIBC(*pu.cu) || (currRefPOC == slice.getRefPOC(eRefPicListIndex, neibRefIdx))))
      {
        Mv pmv = neibMi.mv[eRefPicListIndex];
        pmv.roundTransPrecInternal2Amvr(pu.cu->imv);

#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          info.mvPos[info.numCand]   = neibMi.sourcePos;
          info.mvType[info.numCand]  = MVP_HMVP;
          info.mvSolid[info.numCand] = allCandSolidInAbove && vbOnCtuBoundary; //  cs.isClean(neibMi.soPos, CHANNEL_TYPE_LUMA);
          allCandSolidInAbove = allCandSolidInAbove && vbOnCtuBoundary;
        }
#endif
        info.mvCand[info.numCand++] = pmv;
        if (info.numCand >= AMVP_MAX_NUM_CANDS)
        {
          return;
        }
      }
    }
  }
}

bool PU::isBipredRestriction(const PredictionUnit &pu)
{
  if(pu.cu->lumaSize().width == 4 && pu.cu->lumaSize().height ==4 )
  {
    return true;
  }
  /* disable bi-prediction for 4x8/8x4 */
  if ( pu.cu->lumaSize().width + pu.cu->lumaSize().height == 12 )
  {
    return true;
  }
  return false;
}

#if GDR_ENABLED
void PU::getAffineControlPointCand(const PredictionUnit &pu, MotionInfo mi[4], bool isAvailable[4], int verIdx[4], int8_t bcwIdx, int modelIdx, int verNum, AffineMergeCtx& affMrgType, bool isEncodeGdrClean, bool modelSolid[6])
#else
void PU::getAffineControlPointCand(const PredictionUnit& pu, MotionInfo mi[4], bool isAvailable[4], int verIdx[4], int8_t bcwIdx, int modelIdx, int verNum, AffineMergeCtx& affMrgType)
#endif
{
  int cuW = pu.Y().width;
  int cuH = pu.Y().height;
  int vx, vy;
  int shift = MAX_CU_DEPTH;
  int shiftHtoW = shift + floorLog2(cuW) - floorLog2(cuH);

  // motion info
  Mv cMv[2][4];
  int refIdx[2] = { -1, -1 };
  int dir = 0;
  EAffineModel curType = (verNum == 2) ? AFFINEMODEL_4PARAM : AFFINEMODEL_6PARAM;

  if ( verNum == 2 )
  {
    int idx0 = verIdx[0], idx1 = verIdx[1];
    if ( !isAvailable[idx0] || !isAvailable[idx1] )
    {
      return;
    }

    for ( int l = 0; l < 2; l++ )
    {
      if ( mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0 )
      {
        // check same refidx and different mv
        if ( mi[idx0].refIdx[l] == mi[idx1].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
    }

  }
  else if ( verNum == 3 )
  {
    int idx0 = verIdx[0], idx1 = verIdx[1], idx2 = verIdx[2];
    if ( !isAvailable[idx0] || !isAvailable[idx1] || !isAvailable[idx2] )
    {
      return;
    }

    for ( int l = 0; l < 2; l++ )
    {
      if ( mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0 && mi[idx2].refIdx[l] >= 0 )
      {
        // check same refidx and different mv
        if ( mi[idx0].refIdx[l] == mi[idx1].refIdx[l] && mi[idx0].refIdx[l] == mi[idx2].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
    }

  }

  if ( dir == 0 )
  {
    return;
  }

  for ( int l = 0; l < 2; l++ )
  {
    if ( dir & (l + 1) )
    {
      for ( int i = 0; i < verNum; i++ )
      {
        cMv[l][verIdx[i]] = mi[verIdx[i]].mv[l];
      }

      // convert to LT, RT[, [LB]]
      switch ( modelIdx )
      {
      case 0: // 0 : LT, RT, LB
        break;

      case 1: // 1 : LT, RT, RB
        cMv[l][2].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][1].hor;
        cMv[l][2].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][1].ver;
        cMv[l][2].clipToStorageBitDepth();
        break;

      case 2: // 2 : LT, LB, RB
        cMv[l][1].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][2].hor;
        cMv[l][1].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][2].ver;
        cMv[l][1].clipToStorageBitDepth();
        break;

      case 3: // 3 : RT, LB, RB
        cMv[l][0].hor = cMv[l][1].hor + cMv[l][2].hor - cMv[l][3].hor;
        cMv[l][0].ver = cMv[l][1].ver + cMv[l][2].ver - cMv[l][3].ver;
        cMv[l][0].clipToStorageBitDepth();
        break;

      case 4: // 4 : LT, RT
        break;

      case 5: // 5 : LT, LB
        vx = (cMv[l][0].hor * (1 << shift)) + ((cMv[l][2].ver - cMv[l][0].ver) * (1 << shiftHtoW));
        vy = (cMv[l][0].ver * (1 << shift)) - ((cMv[l][2].hor - cMv[l][0].hor) * (1 << shiftHtoW));
        cMv[l][1].set( vx, vy );
        cMv[l][1].roundAffine(shift);
        cMv[l][1].clipToStorageBitDepth();
        break;

      default: THROW("Invalid model index!"); break;
      }
    }
    else
    {
      for ( int i = 0; i < 4; i++ )
      {
        cMv[l][i].hor = 0;
        cMv[l][i].ver = 0;
      }
    }
  }

  for ( int i = 0; i < 3; i++ )
  {
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].mv = cMv[0][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].refIdx = refIdx[0];

    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].mv = cMv[1][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].refIdx = refIdx[1];
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      affMrgType.mvSolid[(affMrgType.numValidMergeCand << 1) + 0][i] = modelSolid[modelIdx];
      affMrgType.mvSolid[(affMrgType.numValidMergeCand << 1) + 1][i] = modelSolid[modelIdx];
    }
#endif
  }
  affMrgType.interDirNeighbours[affMrgType.numValidMergeCand] = dir;
  affMrgType.affineType[affMrgType.numValidMergeCand]         = curType;
  affMrgType.bcwIdx[affMrgType.numValidMergeCand]             = (dir == 3) ? bcwIdx : BCW_DEFAULT;

  affMrgType.numValidMergeCand++;

  return;
}

#if POSE
#if GDR_ENABLED
void PU::getposeControlPointCand(const PredictionUnit& pu, MotionInfo mi[4], bool isAvailable[4], int verIdx[4], int8_t bcwIdx, int modelIdx, int verNum, AffineMergeCtx& affMrgType, bool isEncodeGdrClean, bool modelSolid[6])
#else
void PU::getposeControlPointCand(const PredictionUnit& pu, MotionInfo mi[4], bool isAvailable[4], int verIdx[4], int8_t bcwIdx, int modelIdx, int verNum, AffineMergeCtx& affMrgType)
#endif
{
  int cuW = pu.Y().width;
  int cuH = pu.Y().height;
  int shift     = MAX_CU_DEPTH;
  int shiftHtoW = shift + floorLog2(cuW) - floorLog2(cuH);
  int vx, vy;
  // motion info
  Mv           cMv[2][4];
  int          refIdx[2] = { -1, -1 };
  int          dir       = 0;
  EAffineModel curType   = (verNum == 2) ? AFFINEMODEL_4PARAM : AFFINEMODEL_6PARAM;


  if (verNum == 2)
  {
    int idx0 = verIdx[0], idx1 = verIdx[1];
    if (!isAvailable[idx0] || !isAvailable[idx1])
    {
      return;
    }

    for (int l = 0; l < 2; l++)
    {
      if (mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0)
      {
        // check same refidx and different mv
        if (mi[idx0].refIdx[l] == mi[idx1].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
    }

  }
  else if (verNum == 3)
  {
    int idx0 = verIdx[0], idx1 = verIdx[1], idx2 = verIdx[2];
    if (!isAvailable[idx0] || !isAvailable[idx1] || !isAvailable[idx2])
    {
      return;
    }

    for (int l = 0; l < 2; l++)
    {
      if (mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0 && mi[idx2].refIdx[l] >= 0)
      {
        // check same refidx and different mv
        if (mi[idx0].refIdx[l] == mi[idx1].refIdx[l] && mi[idx0].refIdx[l] == mi[idx2].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
    }

  }

  if (dir == 0)
  {
    return;
  }

  for (int l = 0; l < 2; l++)
  {
    if (dir & (l + 1))
    {
      for (int i = 0; i < verNum; i++)
      {
        cMv[l][verIdx[i]] = mi[verIdx[i]].mv[l];
      }

      // convert to LT, RT[, [LB]]
      switch (modelIdx)
      {
      case 0: // 0 : LT, RT, LB
        break;

      case 1: // 1 : LT, RT, RB
        cMv[l][2].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][1].hor;
        cMv[l][2].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][1].ver;
        cMv[l][2].clipToStorageBitDepth();
        break;

      case 2: // 2 : LT, LB, RB
        cMv[l][1].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][2].hor;
        cMv[l][1].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][2].ver;
        cMv[l][1].clipToStorageBitDepth();
        break;

      case 3: // 3 : RT, LB, RB
        cMv[l][0].hor = cMv[l][1].hor + cMv[l][2].hor - cMv[l][3].hor;
        cMv[l][0].ver = cMv[l][1].ver + cMv[l][2].ver - cMv[l][3].ver;
        cMv[l][0].clipToStorageBitDepth();
        break;

      case 4: // 4 : LT, RT
        break;

      case 5: // 5 : LT, LB
        vx = (cMv[l][0].hor * (1 << shift)) + ((cMv[l][2].ver - cMv[l][0].ver) * (1 << shiftHtoW));
        vy = (cMv[l][0].ver * (1 << shift)) - ((cMv[l][2].hor - cMv[l][0].hor) * (1 << shiftHtoW));
        cMv[l][1].set(vx, vy);
        cMv[l][1].roundAffine(shift);
        cMv[l][1].clipToStorageBitDepth();
        break;

      default:
        THROW("Invalid model index!");
      }
    }
    else
    {
      for (int i = 0; i < 4; i++)
      {
        cMv[l][i].hor = 0;
        cMv[l][i].ver = 0;
      }
    }
  }

#if CTRL_COMB
  for (int k = POSE_RT_LB; k < POSE_MODEL_NUM; k++)
  {
    affMrgType.poseType[affMrgType.numValidMergeCand] = (EPoseModel)k;
    for (int i = 0; i < 3; i++)
    {
      if (i == k)
      {
        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 0][i].setMvField(Mv(0, 0), -1);
        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 1][i].setMvField(Mv(0, 0), -1);

#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgType.mvSolid[((affMrgType.numValidMergeCand) << 1) + 0][i] = false;
          affMrgType.mvSolid[((affMrgType.numValidMergeCand) << 1) + 1][i] = false;
        }
#endif
      }
      else
      {
        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 0][i].mv = cMv[0][i];
        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 0][i].refIdx = refIdx[0];

        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 1][i].mv = cMv[1][i];
        affMrgType.mvFieldNeighbours[((affMrgType.numValidMergeCand) << 1) + 1][i].refIdx = refIdx[1];
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgType.mvSolid[((affMrgType.numValidMergeCand) << 1) + 0][i] = modelSolid[modelIdx];
          affMrgType.mvSolid[((affMrgType.numValidMergeCand) << 1) + 1][i] = modelSolid[modelIdx];
        }
#endif
      }
    }
    affMrgType.interDirNeighbours[affMrgType.numValidMergeCand] = dir;
    affMrgType.affineType[affMrgType.numValidMergeCand] = curType;
    affMrgType.bcwIdx[affMrgType.numValidMergeCand] = (dir == 3) ? bcwIdx : BCW_DEFAULT;
    affMrgType.numValidMergeCand++;
  }
#else
  for (int i = 0; i < 3; i++)
  {
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].mv     = cMv[0][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].refIdx = refIdx[0];

    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].mv     = cMv[1][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].refIdx = refIdx[1];
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      affMrgType.mvSolid[(affMrgType.numValidMergeCand << 1) + 0][i] = modelSolid[modelIdx];
      affMrgType.mvSolid[(affMrgType.numValidMergeCand << 1) + 1][i] = modelSolid[modelIdx];
    }
#endif
  }
  affMrgType.interDirNeighbours[affMrgType.numValidMergeCand] = dir;
  affMrgType.affineType[affMrgType.numValidMergeCand]         = curType;
  affMrgType.bcwIdx[affMrgType.numValidMergeCand]             = (dir == 3) ? bcwIdx : BCW_DEFAULT;

  affMrgType.numValidMergeCand++;
#endif
  return;
}
#endif

const int getAvailableAffineNeighboursForLeftPredictor( const PredictionUnit &pu, const PredictionUnit* npu[] )
{
  const Position posLB = pu.Y().bottomLeft();
  int num = 0;
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;

  const PredictionUnit *puLeftBottom = pu.cs->getPURestricted( posLB.offset( -1, 1 ), pu, pu.chType );
  if (puLeftBottom && puLeftBottom->cu->affine && puLeftBottom->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 1), plevel))
  {
    npu[num++] = puLeftBottom;
    return num;
  }

  const PredictionUnit* puLeft = pu.cs->getPURestricted( posLB.offset( -1, 0 ), pu, pu.chType );
  if (puLeft && puLeft->cu->affine && puLeft->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 0), plevel))
  {
    npu[num++] = puLeft;
    return num;
  }

  return num;
}

const int getAvailableAffineNeighboursForAbovePredictor( const PredictionUnit &pu, const PredictionUnit* npu[], int numAffNeighLeft )
{
  const Position posLT = pu.Y().topLeft();
  const Position posRT = pu.Y().topRight();
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;
  int num = numAffNeighLeft;

  const PredictionUnit* puAboveRight = pu.cs->getPURestricted( posRT.offset( 1, -1 ), pu, pu.chType );
  if (puAboveRight && puAboveRight->cu->affine && puAboveRight->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posRT.offset(1, -1), plevel))
  {
    npu[num++] = puAboveRight;
    return num;
  }

  const PredictionUnit* puAbove = pu.cs->getPURestricted( posRT.offset( 0, -1 ), pu, pu.chType );
  if (puAbove && puAbove->cu->affine && puAbove->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posRT.offset(0, -1), plevel))
  {
    npu[num++] = puAbove;
    return num;
  }

  const PredictionUnit *puAboveLeft = pu.cs->getPURestricted( posLT.offset( -1, -1 ), pu, pu.chType );
  if (puAboveLeft && puAboveLeft->cu->affine && puAboveLeft->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLT.offset(-1, -1), plevel))
  {
    npu[num++] = puAboveLeft;
    return num;
  }

  return num;
}

#if POSE

const int getAvailableposeNeighboursForLeftPredictor(const PredictionUnit &pu, const PredictionUnit *npu[])
{
  const Position posLB  = pu.Y().bottomLeft();
  int            num    = 0;
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;

  const PredictionUnit *puLeftBottom = pu.cs->getPURestricted(posLB.offset(-1, 1), pu, pu.chType);
  if (puLeftBottom && (puLeftBottom->cu->poseFlag || puLeftBottom->cu->affine)
      && puLeftBottom->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 1), plevel))
  {
    npu[num++] = puLeftBottom;
    return num;
  }

  const PredictionUnit *puLeft = pu.cs->getPURestricted(posLB.offset(-1, 0), pu, pu.chType);
  if (puLeft && (puLeft->cu->poseFlag || puLeft->cu->affine) && puLeft->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 0), plevel))
  {
    npu[num++] = puLeft;
    return num;
  }

  return num;
}

const int getAvailableposeNeighboursForAbovePredictor(const PredictionUnit &pu, const PredictionUnit *npu[],
                                                      int numAffNeighLeft)

{
  const Position posLT  = pu.Y().topLeft();
  const Position posRT  = pu.Y().topRight();
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;
  int            num    = numAffNeighLeft;

  const PredictionUnit *puAboveRight = pu.cs->getPURestricted(posRT.offset(1, -1), pu, pu.chType);
  if (puAboveRight && (puAboveRight->cu->poseFlag||puAboveRight->cu->affine) && puAboveRight->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posRT.offset(1, -1), plevel))
  {
    npu[num++] = puAboveRight;
    return num;
  }

  const PredictionUnit *puAbove = pu.cs->getPURestricted(posRT.offset(0, -1), pu, pu.chType);
  if (puAbove && (puAbove->cu->poseFlag || puAbove->cu->affine) && puAbove->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posRT.offset(0, -1), plevel))
  {
    npu[num++] = puAbove;
    return num;
  }

  const PredictionUnit *puAboveLeft = pu.cs->getPURestricted(posLT.offset(-1, -1), pu, pu.chType);
  if (puAboveLeft && (puAboveLeft->cu->poseFlag || puAboveLeft->cu->affine)&& puAboveLeft->mergeType == MRG_TYPE_DEFAULT_N
      && PU::isDiffMER(pu.lumaPos(), posLT.offset(-1, -1), plevel))
  {
    npu[num++] = puAboveLeft;
    return num;
  }

  return num;
}
#endif 

void PU::getAffineMergeCand( const PredictionUnit &pu, AffineMergeCtx& affMrgCtx, const int mrgCandIdx )
{
  const CodingStructure &cs = *pu.cs;
  const Slice &slice = *pu.cs->slice;
  const uint32_t maxNumAffineMergeCand = slice.getPicHeader()->getMaxNumAffineMergeCand();
  const unsigned plevel = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;
#if GDR_ENABLED
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif

  for ( int i = 0; i < maxNumAffineMergeCand; i++ )
  {
    for ( int mvNum = 0; mvNum < 3; mvNum++ )
    {
      affMrgCtx.mvFieldNeighbours[(i << 1) + 0][mvNum].setMvField( Mv(), -1 );
      affMrgCtx.mvFieldNeighbours[(i << 1) + 1][mvNum].setMvField( Mv(), -1 );
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        affMrgCtx.mvSolid[(i << 1) + 0][mvNum] = true;
        affMrgCtx.mvSolid[(i << 1) + 1][mvNum] = true;
        affMrgCtx.mvValid[(i << 1) + 0][mvNum] = true;
        affMrgCtx.mvValid[(i << 1) + 1][mvNum] = true;
      }
#endif
    }
    affMrgCtx.interDirNeighbours[i] = 0;
    affMrgCtx.affineType[i]         = AFFINEMODEL_4PARAM;
    affMrgCtx.mergeType[i]          = MRG_TYPE_DEFAULT_N;
    affMrgCtx.bcwIdx[i]             = BCW_DEFAULT;
  }
#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    MergeCtx &mrgCtx = *affMrgCtx.mrgCtx;
    int numMergeCand = MRG_MAX_NUM_CANDS << 1;
    for (int i = 0; i < numMergeCand; i++)
    {
      mrgCtx.mvSolid[i] = true;
      mrgCtx.mvValid[i] = true;
    }
  }
#endif

  affMrgCtx.numValidMergeCand = 0;
  affMrgCtx.maxNumMergeCand = maxNumAffineMergeCand;

  bool sbTmvpEnableFlag = slice.getSPS()->getSbTMVPEnabledFlag()
                          && !(slice.getPOC() == slice.getRefPic(REF_PIC_LIST_0, 0)->getPOC() && slice.isIRAP());
  bool isAvailableSubPu = false;
  if (sbTmvpEnableFlag && slice.getPicHeader()->getEnableTMVPFlag())
  {
    MergeCtx mrgCtx = *affMrgCtx.mrgCtx;

    CHECK( mrgCtx.subPuMvpMiBuf.area() == 0 || !mrgCtx.subPuMvpMiBuf.buf, "Buffer not initialized" );
    mrgCtx.subPuMvpMiBuf.fill( MotionInfo() );

    int pos = 0;
    // Get spatial MV
    const Position posCurLB = pu.Y().bottomLeft();
    MotionInfo miLeft;

    //left
    const PredictionUnit* puLeft = cs.getPURestricted( posCurLB.offset( -1, 0 ), pu, pu.chType );
    const bool isAvailableA1 = puLeft && isDiffMER(pu.lumaPos(), posCurLB.offset(-1, 0), plevel) && pu.cu != puLeft->cu && CU::isInter( *puLeft->cu );
    if ( isAvailableA1 )
    {
      miLeft = puLeft->getMotionInfo( posCurLB.offset( -1, 0 ) );
      // get Inter Dir
      mrgCtx.interDirNeighbours[pos] = miLeft.interDir;

      // get Mv from Left
      mrgCtx.mvFieldNeighbours[pos << 1].setMvField( miLeft.mv[0], miLeft.refIdx[0] );

      if ( slice.isInterB() )
      {
        mrgCtx.mvFieldNeighbours[(pos << 1) + 1].setMvField( miLeft.mv[1], miLeft.refIdx[1] );
      }
#if GDR_ENABLED
      // check if the (puLeft) is in clean area
      if (isEncodeGdrClean)
      {
        mrgCtx.mvSolid[(pos << 1) + 0] = cs.isClean(puLeft->Y().bottomRight(), CHANNEL_TYPE_LUMA);
        mrgCtx.mvSolid[(pos << 1) + 1] = cs.isClean(puLeft->Y().bottomRight(), CHANNEL_TYPE_LUMA);
      }
#endif
      pos++;
    }

    mrgCtx.numValidMergeCand = pos;

    isAvailableSubPu = getInterMergeSubPuMvpCand(pu, mrgCtx, pos, 0);
    if ( isAvailableSubPu )
    {
      for ( int mvNum = 0; mvNum < 3; mvNum++ )
      {
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum].setMvField( mrgCtx.mvFieldNeighbours[(pos << 1) + 0].mv, mrgCtx.mvFieldNeighbours[(pos << 1) + 0].refIdx );
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum].setMvField( mrgCtx.mvFieldNeighbours[(pos << 1) + 1].mv, mrgCtx.mvFieldNeighbours[(pos << 1) + 1].refIdx );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum] = mrgCtx.mvSolid[(pos << 1) + 0];
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum] = mrgCtx.mvSolid[(pos << 1) + 1];
        }
#endif
      }
      affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = mrgCtx.interDirNeighbours[pos];

      affMrgCtx.affineType[affMrgCtx.numValidMergeCand] = AFFINE_MODEL_NUM;
      affMrgCtx.mergeType[affMrgCtx.numValidMergeCand] = MRG_TYPE_SUBPU_ATMVP;
      if ( affMrgCtx.numValidMergeCand == mrgCandIdx )
      {
        return;
      }

      affMrgCtx.numValidMergeCand++;

      // early termination
      if ( affMrgCtx.numValidMergeCand == maxNumAffineMergeCand )
      {
        return;
      }
    }
  }

  if ( slice.getSPS()->getUseAffine() )
  {
    ///> Start: inherited affine candidates
    const PredictionUnit* npu[5];
    int numAffNeighLeft = getAvailableAffineNeighboursForLeftPredictor( pu, npu );
    int numAffNeigh = getAvailableAffineNeighboursForAbovePredictor( pu, npu, numAffNeighLeft );
    for ( int idx = 0; idx < numAffNeigh; idx++ )
    {
      // derive Mv from Neigh affine PU
      Mv cMv[2][3];
#if GDR_ENABLED
      bool    cMvSolid[2][3] = { {true, true, true}, {true, true, true} };
      MvpType cMvType[2][3];
      Position cMvPos[2][3];
#endif
      const PredictionUnit* puNeigh = npu[idx];
      pu.cu->affineType = puNeigh->cu->affineType;
      if ( puNeigh->interDir != 2 )
      {
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0], cMvSolid[0], cMvType[0], cMvPos[0]);
        }
        else
        {
          xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0]);
        }
#else
        xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0]);
#endif
      }
      if ( slice.isInterB() )
      {
        if ( puNeigh->interDir != 1 )
        {
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1], cMvSolid[1], cMvType[1], cMvPos[1]);
          }
          else
          {
            xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1]);
          }
#else
          xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1]);
#endif
        }
      }

      for ( int mvNum = 0; mvNum < 3; mvNum++ )
      {
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum].setMvField( cMv[0][mvNum], puNeigh->refIdx[0] );
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum].setMvField( cMv[1][mvNum], puNeigh->refIdx[1] );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum] = cMvSolid[0][mvNum];
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum] = cMvSolid[0][mvNum];
        }
#endif
      }
      affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = puNeigh->interDir;
      affMrgCtx.affineType[affMrgCtx.numValidMergeCand]         = (EAffineModel) (puNeigh->cu->affineType);
      affMrgCtx.bcwIdx[affMrgCtx.numValidMergeCand]             = puNeigh->cu->bcwIdx;

      if ( affMrgCtx.numValidMergeCand == mrgCandIdx )
      {
        return;
      }

      // early termination
      affMrgCtx.numValidMergeCand++;
      if ( affMrgCtx.numValidMergeCand == maxNumAffineMergeCand )
      {
        return;
      }
    }
    ///> End: inherited affine candidates

    ///> Start: Constructed affine candidates
    {
      MotionInfo mi[4];
      bool isAvailable[4] = { false };

      int8_t neighBcw[2] = { BCW_DEFAULT, BCW_DEFAULT };
      // control point: LT B2->B3->A2
      const Position posLT[3] = { pu.Y().topLeft().offset( -1, -1 ), pu.Y().topLeft().offset( 0, -1 ), pu.Y().topLeft().offset( -1, 0 ) };
#if GDR_ENABLED
      bool miSolid[4] = { false, false, false, false };
#endif
      for ( int i = 0; i < 3; i++ )
      {
        const Position pos = posLT[i];
        const PredictionUnit* puNeigh = cs.getPURestricted( pos, pu, pu.chType );

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[0] = true;
          mi[0]          = puNeigh->getMotionInfo(pos);
          neighBcw[0]    = puNeigh->cu->bcwIdx;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[0] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: RT B1->B0
      const Position posRT[2] = { pu.Y().topRight().offset( 0, -1 ), pu.Y().topRight().offset( 1, -1 ) };
      for ( int i = 0; i < 2; i++ )
      {
        const Position pos = posRT[i];
        const PredictionUnit* puNeigh = cs.getPURestricted( pos, pu, pu.chType );

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[1] = true;
          mi[1]          = puNeigh->getMotionInfo(pos);
          neighBcw[1]    = puNeigh->cu->bcwIdx;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[1] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: LB A1->A0
      const Position posLB[2] = { pu.Y().bottomLeft().offset( -1, 0 ), pu.Y().bottomLeft().offset( -1, 1 ) };
      for ( int i = 0; i < 2; i++ )
      {
        const Position pos = posLB[i];
        const PredictionUnit* puNeigh = cs.getPURestricted( pos, pu, pu.chType );

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[2] = true;
          mi[2] = puNeigh->getMotionInfo( pos );
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[2] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: RB
      if ( slice.getPicHeader()->getEnableTMVPFlag() )
      {
        //>> MTK colocated-RightBottom
        // offset the pos to be sure to "point" to the same position the uiAbsPartIdx would've pointed to
        Position posRB = pu.Y().bottomRight().offset( -3, -3 );

        const PreCalcValues& pcv = *cs.pcv;
        Position posC0;
        bool C0Avail = false;

        bool boundaryCond = ((posRB.x + pcv.minCUWidth) < pcv.lumaWidth) && ((posRB.y + pcv.minCUHeight) < pcv.lumaHeight);
        const SubPic &curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
        if (curSubPic.getTreatedAsPicFlag())
        {
          boundaryCond = ((posRB.x + pcv.minCUWidth) <= curSubPic.getSubPicRight() &&
            (posRB.y + pcv.minCUHeight) <= curSubPic.getSubPicBottom());
        }
        if (boundaryCond)
        {
          int posYInCtu = posRB.y & pcv.maxCUHeightMask;
          if (posYInCtu + 4 < pcv.maxCUHeight)
          {
            posC0 = posRB.offset(4, 4);
            C0Avail = true;
          }
        }

        Mv   cColMv;
        int  refIdx  = 0;
        bool existMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, cColMv, refIdx, false);

        if (existMV)
        {
          mi[3].mv[0] = cColMv;
          mi[3].refIdx[0] = refIdx;
          mi[3].interDir = 1;
          isAvailable[3] = true;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            bool posL0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
            bool posL0inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_0, refIdx);

            miSolid[3] = posL0inCurPicSolid && posL0inRefPicSolid;
          }
#endif
        }

        if ( slice.isInterB() )
        {
          existMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, cColMv, refIdx, false);
          if (existMV)
          {
            mi[3].mv[1] = cColMv;
            mi[3].refIdx[1] = refIdx;
            mi[3].interDir |= 2;
            isAvailable[3] = true;
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              bool posL1inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
              bool posL1inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_1, refIdx);

              miSolid[3] = (mi[3].interDir & 1) ? (miSolid[3] && posL1inCurPicSolid && posL1inRefPicSolid) : (posL1inCurPicSolid && posL1inRefPicSolid);
            }
#endif
          }
        }
      }

      //-------------------  insert model  -------------------//
      int order[6] = { 0, 1, 2, 3, 4, 5 };
      int modelNum = 6;
      int model[6][4] = {
        { 0, 1, 2 },          // 0:  LT, RT, LB
        { 0, 1, 3 },          // 1:  LT, RT, RB
        { 0, 2, 3 },          // 2:  LT, LB, RB
        { 1, 2, 3 },          // 3:  RT, LB, RB
        { 0, 1 },             // 4:  LT, RT
        { 0, 2 },             // 5:  LT, LB
      };

#if GDR_ENABLED
      bool modelSolid[6] =
      {
        miSolid[0] && miSolid[1] && miSolid[2],
        miSolid[0] && miSolid[1] && miSolid[3],
        miSolid[0] && miSolid[2] && miSolid[3],
        miSolid[1] && miSolid[2] && miSolid[3],
        miSolid[0] && miSolid[1],
        miSolid[0] && miSolid[2]
      };
#endif
      int verNum[6] = { 3, 3, 3, 3, 2, 2 };
      int startIdx = pu.cs->sps->getUseAffineType() ? 0 : 4;
      for ( int idx = startIdx; idx < modelNum; idx++ )
      {
        int modelIdx = order[idx];
#if GDR_ENABLED
        getAffineControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]), modelIdx, verNum[modelIdx], affMrgCtx, isEncodeGdrClean, modelSolid);
#else
        getAffineControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]), modelIdx, verNum[modelIdx], affMrgCtx);
#endif
        if ( affMrgCtx.numValidMergeCand != 0 && affMrgCtx.numValidMergeCand - 1 == mrgCandIdx )
        {
          return;
        }

        // early termination
        if ( affMrgCtx.numValidMergeCand == maxNumAffineMergeCand )
        {
          return;
        }
      }
    }
    ///> End: Constructed affine candidates
  }

  ///> zero padding
  int cnt = affMrgCtx.numValidMergeCand;
  while ( cnt < maxNumAffineMergeCand )
  {
    for ( int mvNum = 0; mvNum < 3; mvNum++ )
    {
      affMrgCtx.mvFieldNeighbours[(cnt << 1) + 0][mvNum].setMvField( Mv( 0, 0 ), 0 );
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        affMrgCtx.mvSolid[(cnt << 1) + 0][mvNum] = true;
      }
#endif
    }
    affMrgCtx.interDirNeighbours[cnt] = 1;

    if ( slice.isInterB() )
    {
      for ( int mvNum = 0; mvNum < 3; mvNum++ )
      {
        affMrgCtx.mvFieldNeighbours[(cnt << 1) + 1][mvNum].setMvField( Mv( 0, 0 ), 0 );
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgCtx.mvSolid[(cnt << 1) + 1][mvNum] = true;
        }
#endif
      }
      affMrgCtx.interDirNeighbours[cnt] = 3;
    }
    affMrgCtx.affineType[cnt] = AFFINEMODEL_4PARAM;

    if ( cnt == mrgCandIdx )
    {
      return;
    }
    cnt++;
    affMrgCtx.numValidMergeCand++;
  }
}
#if IDEAL_SITU1 
void PU::getIdealPoseMergeCand(const PredictionUnit& pu, AffineMergeCtx& affMrgCtx, const int mrgCandIdx)
{
  double picW = pu.cs->picture->lwidth(), picH = pu.cs->picture->lheight();
  double Ox = (picW - 1) / 2.0, Oy = (picH - 1) / 2.0;
  int x0 = pu.Y().x, y0 = pu.Y().y;
  int x1 = pu.Y().topRight().x, y1 = pu.Y().topRight().y;
  int x2 = pu.Y().bottomLeft().x, y2 = pu.Y().bottomLeft().y;
  double x01 = x0 - Ox, y01 = x0 - Oy, x11 = x1 - Ox, y11 = y1 - Oy, x21 = x2 - Ox, y21 = y2 - Oy;
  int num = 0;
  for (int l = 0; l < 2; l++)
  {
    for (int i = 0; i<pu.cs->slice->getNumRefIdx(RefPicList(l)); i++)
    {
      int refPoc = pu.cs->slice->getRefPOC(RefPicList(l), i);
      int theta_diff_degree = pu.cs->slice->getPOC() - refPoc;
      double theta_diff = degress2rad(theta_diff_degree);
      double x0_ref = (x01 * cos(theta_diff) - y01 * sin(theta_diff)) * 16;
      double y0_ref = (x01 * sin(theta_diff) + y01 * cos(theta_diff)) * 16;
      double x1_ref = (x11 * cos(theta_diff) - y11 * sin(theta_diff)) * 16;
      double y1_ref = (x11 * sin(theta_diff) + y11 * cos(theta_diff)) * 16;
      double x2_ref = (x21 * cos(theta_diff) - y21 * sin(theta_diff)) * 16;
      double y2_ref = (x21 * sin(theta_diff) + y21 * cos(theta_diff)) * 16;

      affMrgCtx.mvFieldNeighbours[(num << 1) + 0][0].setMvField(Mv(x0_ref - x01 * 16, y0_ref - y01 * 16), i);
      affMrgCtx.mvFieldNeighbours[(num << 1) + 0][1].setMvField(Mv(x1_ref - x11 * 16, y1_ref - y11 * 16), i);
      affMrgCtx.mvFieldNeighbours[(num << 1) + 0][2].setMvField(Mv(x2_ref - x21 * 16, y2_ref - y21 * 16), i);

      affMrgCtx.interDirNeighbours[num] = l+1;
      affMrgCtx.mergeType[num] = MRG_TYPE_DEFAULT_N;
      affMrgCtx.bcwIdx[num] = BCW_DEFAULT;
      num++;
    }
  }
  affMrgCtx.numValidMergeCand = num;
}
#endif

#if POSE
void PU::getposeMergeCand(const PredictionUnit &pu, AffineMergeCtx &affMrgCtx, const int mrgCandIdx) 
{
  const CodingStructure &cs                    = *pu.cs;
  const Slice &          slice                 = *pu.cs->slice;
  //const uint32_t         maxNumAffineMergeCand = slice.getPicHeader()->getMaxNumAffineMergeCand();
#if CTRL_COMB
  const uint32_t maxNumAffineMergeCand = POSECANDNUM*3;
#else
  const uint32_t maxNumAffineMergeCand = POSECANDNUM;
#endif
  const unsigned         plevel                = pu.cs->sps->getLog2ParallelMergeLevelMinus2() + 2;
#if GDR_ENABLED
  const bool isEncodeGdrClean =
    cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder
    && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA))
        || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif

  for (int i = 0; i < maxNumAffineMergeCand; i++)
  {
    for (int mvNum = 0; mvNum < 3; mvNum++)
    {
      affMrgCtx.mvFieldNeighbours[(i << 1) + 0][mvNum].setMvField(Mv(), -1);
      affMrgCtx.mvFieldNeighbours[(i << 1) + 1][mvNum].setMvField(Mv(), -1);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        affMrgCtx.mvSolid[(i << 1) + 0][mvNum] = true;
        affMrgCtx.mvSolid[(i << 1) + 1][mvNum] = true;
        affMrgCtx.mvValid[(i << 1) + 0][mvNum] = true;
        affMrgCtx.mvValid[(i << 1) + 1][mvNum] = true;
      }
#endif
    }
    affMrgCtx.interDirNeighbours[i] = 0;

#if CTRL_COMB
    affMrgCtx.poseType[i] = POSE_MODEL_NUM;
#endif // CTRL_COMB

    affMrgCtx.affineType[i]         = AFFINEMODEL_4PARAM;
    affMrgCtx.mergeType[i]          = MRG_TYPE_DEFAULT_N;
    affMrgCtx.bcwIdx[i]             = BCW_DEFAULT;
  }
#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    MergeCtx &mrgCtx       = *affMrgCtx.mrgCtx;
    int       numMergeCand = MRG_MAX_NUM_CANDS << 1;
    for (int i = 0; i < numMergeCand; i++)
    {
      mrgCtx.mvSolid[i] = true;
      mrgCtx.mvValid[i] = true;
    }
  }
#endif

  affMrgCtx.numValidMergeCand = 0;
  affMrgCtx.maxNumMergeCand   = maxNumAffineMergeCand;

  /*删除sbTMVP*/

  
  {
    ///> Start: inherited affine candidates 继承affine和pose编码候选
    const PredictionUnit *npu[5];
    int                   numAffNeighLeft = getAvailableposeNeighboursForLeftPredictor(pu, npu);
    int                   numAffNeigh     = getAvailableposeNeighboursForAbovePredictor(pu, npu, numAffNeighLeft);
    for (int idx = 0; idx < numAffNeigh; idx++)
    {
      // derive Mv from Neigh affine PU
      Mv cMv[2][3];
#if GDR_ENABLED
      bool     cMvSolid[2][3] = { { true, true, true }, { true, true, true } };
      MvpType  cMvType[2][3];
      Position cMvPos[2][3];
#endif
      const PredictionUnit *puNeigh = npu[idx];
      pu.cu->affineType             = puNeigh->cu->affineType;
      if (puNeigh->interDir != 2)
      {
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0], cMvSolid[0], cMvType[0], cMvPos[0]);
        }
        else
        {
          xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0]);
        }
#else
        xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0]);
#endif
      }
      if (slice.isInterB())
      {
        if (puNeigh->interDir != 1)
        {
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1], cMvSolid[1], cMvType[1], cMvPos[1]);
          }
          else
          {
            xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1]);
          }
#else
          xInheritedposeMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1]);
#endif
        }
      }

#if CTRL_COMB
      for (int k = POSE_RT_LB; k < POSE_MODEL_NUM; k++)
      {
        affMrgCtx.poseType[affMrgCtx.numValidMergeCand] = (EPoseModel)k;
        for (int mvNum = 0; mvNum < 3; mvNum++)
        {
          if (mvNum == k)
          {
            affMrgCtx.mvFieldNeighbours[((affMrgCtx.numValidMergeCand) << 1) + 0][mvNum].setMvField(Mv(0, 0), -1);
            affMrgCtx.mvFieldNeighbours[((affMrgCtx.numValidMergeCand) << 1) + 1][mvNum].setMvField(Mv(0, 0), -1);
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              affMrgCtx.mvSolid[((affMrgCtx.numValidMergeCand) << 1) + 0][mvNum] = cMvSolid[0][mvNum];
              affMrgCtx.mvSolid[((affMrgCtx.numValidMergeCand) << 1) + 1][mvNum] = cMvSolid[0][mvNum];
            }
#endif
          }
          else
          {
            affMrgCtx.mvFieldNeighbours[((affMrgCtx.numValidMergeCand) << 1) + 0][mvNum].setMvField(cMv[0][mvNum],
              puNeigh->refIdx[0]);
            affMrgCtx.mvFieldNeighbours[((affMrgCtx.numValidMergeCand) << 1) + 1][mvNum].setMvField(cMv[1][mvNum],
              puNeigh->refIdx[1]);
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              affMrgCtx.mvSolid[((affMrgCtx.numValidMergeCand) << 1) + 0][mvNum] = cMvSolid[0][mvNum];
              affMrgCtx.mvSolid[((affMrgCtx.numValidMergeCand) << 1) + 1][mvNum] = cMvSolid[0][mvNum];
            }
#endif
          }
        }
        affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = puNeigh->interDir;
        affMrgCtx.affineType[affMrgCtx.numValidMergeCand] = (EAffineModel)(puNeigh->cu->affineType);
        affMrgCtx.bcwIdx[affMrgCtx.numValidMergeCand] = puNeigh->cu->bcwIdx;
        if (affMrgCtx.numValidMergeCand == mrgCandIdx)
        {
          return;
        }

        // early termination
        affMrgCtx.numValidMergeCand++;
        if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
        {
          return;
        }
      }
#else
      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum].setMvField(cMv[0][mvNum],
                                                                                              puNeigh->refIdx[0]);
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum].setMvField(cMv[1][mvNum],
                                                                                              puNeigh->refIdx[1]);
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum] = cMvSolid[0][mvNum];
          affMrgCtx.mvSolid[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum] = cMvSolid[0][mvNum];
        }
#endif
      }
      affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = puNeigh->interDir;
      affMrgCtx.affineType[affMrgCtx.numValidMergeCand]         = (EAffineModel) (puNeigh->cu->affineType);
      affMrgCtx.bcwIdx[affMrgCtx.numValidMergeCand]             = puNeigh->cu->bcwIdx;

      if (affMrgCtx.numValidMergeCand == mrgCandIdx)
      {
        return;
      }

      // early termination
      affMrgCtx.numValidMergeCand++;
      if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
      {
        return;
      }
#endif
    }
    ///> End: inherited affine candidates

    ///> Start: Constructed affine candidates
    {
      MotionInfo mi[4];
      bool       isAvailable[4] = { false };

      int8_t neighBcw[2] = { BCW_DEFAULT, BCW_DEFAULT };
      // control point: LT B2->B3->A2
      const Position posLT[3] = { pu.Y().topLeft().offset(-1, -1), pu.Y().topLeft().offset(0, -1),
                                  pu.Y().topLeft().offset(-1, 0) };
#if GDR_ENABLED
      bool miSolid[4] = { false, false, false, false };
#endif
      for (int i = 0; i < 3; i++)
      {
        const Position        pos     = posLT[i];
        const PredictionUnit *puNeigh = cs.getPURestricted(pos, pu, pu.chType);

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[0] = true;
          mi[0]          = puNeigh->getMotionInfo(pos);
          neighBcw[0]    = puNeigh->cu->bcwIdx;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[0] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: RT B1->B0
      const Position posRT[2] = { pu.Y().topRight().offset(0, -1), pu.Y().topRight().offset(1, -1) };
      for (int i = 0; i < 2; i++)
      {
        const Position        pos     = posRT[i];
        const PredictionUnit *puNeigh = cs.getPURestricted(pos, pu, pu.chType);

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[1] = true;
          mi[1]          = puNeigh->getMotionInfo(pos);
          neighBcw[1]    = puNeigh->cu->bcwIdx;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[1] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: LB A1->A0
      const Position posLB[2] = { pu.Y().bottomLeft().offset(-1, 0), pu.Y().bottomLeft().offset(-1, 1) };
      for (int i = 0; i < 2; i++)
      {
        const Position        pos     = posLB[i];
        const PredictionUnit *puNeigh = cs.getPURestricted(pos, pu, pu.chType);

        if (puNeigh && CU::isInter(*puNeigh->cu) && PU::isDiffMER(pu.lumaPos(), pos, plevel))
        {
          isAvailable[2] = true;
          mi[2]          = puNeigh->getMotionInfo(pos);
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            miSolid[2] = cs.isClean(puNeigh->Y().topRight(), CHANNEL_TYPE_LUMA);
          }
#endif
          break;
        }
      }

      // control point: RB
      if (slice.getPicHeader()->getEnableTMVPFlag())
      {
        //>> MTK colocated-RightBottom
        // offset the pos to be sure to "point" to the same position the uiAbsPartIdx would've pointed to
        Position posRB = pu.Y().bottomRight().offset(-3, -3);

        const PreCalcValues &pcv = *cs.pcv;
        Position             posC0;
        bool                 C0Avail = false;

        bool boundaryCond =
          ((posRB.x + pcv.minCUWidth) < pcv.lumaWidth) && ((posRB.y + pcv.minCUHeight) < pcv.lumaHeight);
        const SubPic &curSubPic = pu.cs->slice->getPPS()->getSubPicFromPos(pu.lumaPos());
        if (curSubPic.getTreatedAsPicFlag())
        {
          boundaryCond = ((posRB.x + pcv.minCUWidth) <= curSubPic.getSubPicRight()
                          && (posRB.y + pcv.minCUHeight) <= curSubPic.getSubPicBottom());
        }
        if (boundaryCond)
        {
          int posYInCtu = posRB.y & pcv.maxCUHeightMask;
          if (posYInCtu + 4 < pcv.maxCUHeight)
          {
            posC0   = posRB.offset(4, 4);
            C0Avail = true;
          }
        }

        Mv   cColMv;
        int  refIdx  = 0;
        bool existMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, cColMv, refIdx, false);

        if (existMV)
        {
          mi[3].mv[0]     = cColMv;
          mi[3].refIdx[0] = refIdx;
          mi[3].interDir  = 1;
          isAvailable[3]  = true;
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            bool posL0inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
            bool posL0inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_0, refIdx);

            miSolid[3] = posL0inCurPicSolid && posL0inRefPicSolid;
          }
#endif
        }

        if (slice.isInterB())
        {
          existMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, cColMv, refIdx, false);
          if (existMV)
          {
            mi[3].mv[1]     = cColMv;
            mi[3].refIdx[1] = refIdx;
            mi[3].interDir |= 2;
            isAvailable[3] = true;
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              bool posL1inCurPicSolid = cs.isClean(posC0, CHANNEL_TYPE_LUMA);
              bool posL1inRefPicSolid = cs.isClean(posC0, REF_PIC_LIST_1, refIdx);

              miSolid[3] = (mi[3].interDir & 1) ? (miSolid[3] && posL1inCurPicSolid && posL1inRefPicSolid)
                                                : (posL1inCurPicSolid && posL1inRefPicSolid);
            }
#endif
          }
        }
      }

      //-------------------  insert model  -------------------//
      int order[6]    = { 0, 1, 2, 3, 4, 5 };
      int modelNum    = 6;
      int model[6][4] = {
        { 0, 1, 2 },   // 0:  LT, RT, LB
        { 0, 1, 3 },   // 1:  LT, RT, RB
        { 0, 2, 3 },   // 2:  LT, LB, RB
        { 1, 2, 3 },   // 3:  RT, LB, RB
        { 0, 1 },      // 4:  LT, RT
        { 0, 2 },      // 5:  LT, LB
      };

#if GDR_ENABLED
      bool modelSolid[6] = { miSolid[0] && miSolid[1] && miSolid[2],
                             miSolid[0] && miSolid[1] && miSolid[3],
                             miSolid[0] && miSolid[2] && miSolid[3],
                             miSolid[1] && miSolid[2] && miSolid[3],
                             miSolid[0] && miSolid[1],
                             miSolid[0] && miSolid[2] };
#endif
      int verNum[6] = { 3, 3, 3, 3, 2, 2 };
      //int startIdx  = pu.cs->sps->getUseAffineType() ? 0 : 4;  //CY 曾经的代码，好像有问题
      int startIdx = 0;//CY 修改后的代码，这时编解码一致

      for (int idx = startIdx; idx < modelNum; idx++)
      {
        int modelIdx = order[idx];
#if CTRL_POINT
        if (idx == 4) continue;
#endif

#if GDR_ENABLED
#if CTRL_COMB
        getposeControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]),
                                  modelIdx, verNum[modelIdx], affMrgCtx, isEncodeGdrClean, modelSolid);  //CY problem 原代码 注释掉这里试一下，编解码应该会一致
#else
        getAffineControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]),
          modelIdx, verNum[modelIdx], affMrgCtx, isEncodeGdrClean, modelSolid);  //CY problem 原代码 注释掉这里试一下，编解码应该会一致
#endif
#else
#if CTRL_COMB
        getposeControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]),
          modelIdx, verNum[modelIdx], affMrgCtx, isEncodeGdrClean, modelSolid);  //CY problem 原代码 注释掉这里试一下，编解码应该会一致
#else
        getAffineControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]),
          modelIdx, verNum[modelIdx], affMrgCtx);
#endif

#endif
        if (affMrgCtx.numValidMergeCand != 0 && affMrgCtx.numValidMergeCand - 1 == mrgCandIdx)
        {
          return;
        }
        // early termination
        if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
        {
          return;
        }
      }
    }
    ///> End: Constructed affine candidates
  }

  ///> zero padding
  int cnt = affMrgCtx.numValidMergeCand;
  while (cnt < maxNumAffineMergeCand)
  {
#if CTRL_COMB
    for (int k = POSE_RT_LB; k < POSE_MODEL_NUM; k++)
    {
      affMrgCtx.poseType[cnt] = (EPoseModel)k;
      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        if (mvNum == k)
        {
          affMrgCtx.mvFieldNeighbours[((cnt) << 1) + 0][mvNum].setMvField(Mv(0, 0), -1);
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            affMrgCtx.mvSolid[((cnt) << 1) + 0][mvNum] = false;
          }
#endif
        }
        else
        {
          affMrgCtx.mvFieldNeighbours[((cnt) << 1) + 0][mvNum].setMvField(Mv(0, 0), 0);
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            affMrgCtx.mvSolid[((cnt) << 1) + 0][mvNum] = true;
          }
#endif
        }
      }
      affMrgCtx.interDirNeighbours[cnt] = 1;
      if (slice.isInterB())
      {
        for (int mvNum = 0; mvNum < 3; mvNum++)
        {
          if (mvNum == k)
          {
            affMrgCtx.mvFieldNeighbours[((cnt) << 1) + 1][mvNum].setMvField(Mv(0, 0), -1);
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              affMrgCtx.mvSolid[((cnt) << 1) + 1][mvNum] = false;
            }
#endif
          }
          else
          {
            affMrgCtx.mvFieldNeighbours[((cnt) << 1) + 1][mvNum].setMvField(Mv(0, 0), 0);
#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              affMrgCtx.mvSolid[((cnt) << 1) + 1][mvNum] = true;
            }
#endif
          }
        }
        affMrgCtx.interDirNeighbours[cnt] = 3;
      }
      affMrgCtx.affineType[cnt] = AFFINEMODEL_4PARAM;
      if (cnt == mrgCandIdx)
      {
        return;
      }
      cnt++;
      affMrgCtx.numValidMergeCand++;
    }
#else
    for (int mvNum = 0; mvNum < 3; mvNum++)
    {
      affMrgCtx.mvFieldNeighbours[(cnt << 1) + 0][mvNum].setMvField(Mv(0, 0), 0);
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        affMrgCtx.mvSolid[(cnt << 1) + 0][mvNum] = true;
      }
#endif
    }
    affMrgCtx.interDirNeighbours[cnt] = 1;

    if (slice.isInterB())
    {
      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        affMrgCtx.mvFieldNeighbours[(cnt << 1) + 1][mvNum].setMvField(Mv(0, 0), 0);
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          affMrgCtx.mvSolid[(cnt << 1) + 1][mvNum] = true;
        }
#endif
      }
      affMrgCtx.interDirNeighbours[cnt] = 3;
    }
    affMrgCtx.affineType[cnt] = AFFINEMODEL_4PARAM;

    if (cnt == mrgCandIdx)
    {
      return;
    }
    cnt++;
    affMrgCtx.numValidMergeCand++;
#endif
  }
}
#endif

void PU::setAllAffineMvField( PredictionUnit &pu, MvField *mvField, RefPicList eRefList 
#if DATA_DIST_COST
  , int rdo
#endif
)
{
  // Set Mv
  Mv mv[3];
  for ( int i = 0; i < 3; i++ )
  {
    mv[i] = mvField[i].mv;
  }
#if DATA_DIST_COST
  setAllAffineMv(pu, mv[0], mv[1], mv[2], eRefList,false,rdo);
#else
  setAllAffineMv( pu, mv[0], mv[1], mv[2], eRefList );
#endif

  // Set RefIdx
  CHECK( mvField[0].refIdx != mvField[1].refIdx || mvField[0].refIdx != mvField[2].refIdx, "Affine mv corners don't have the same refIdx." );
  pu.refIdx[eRefList] = mvField[0].refIdx;
}

void PU::setAllAffineMv(PredictionUnit& pu, Mv affLT, Mv affRT, Mv affLB, RefPicList eRefList, bool clipCPMVs
#if DATA_DIST_COST
  , int rdo
#endif
)
{
  int width  = pu.Y().width;
  int shift = MAX_CU_DEPTH;
  if (clipCPMVs)
  {
    affLT.mvCliptoStorageBitDepth();
    affRT.mvCliptoStorageBitDepth();
    if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      affLB.mvCliptoStorageBitDepth();
    }
  }
  int deltaMvHorX, deltaMvHorY, deltaMvVerX, deltaMvVerY;
  deltaMvHorX = (affRT - affLT).getHor() * (1 << (shift - floorLog2(width)));
  deltaMvHorY = (affRT - affLT).getVer() * (1 << (shift - floorLog2(width)));

  int height = pu.Y().height;
  if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
  {
    deltaMvVerX = (affLB - affLT).getHor() * (1 << (shift - floorLog2(height)));
    deltaMvVerY = (affLB - affLT).getVer() * (1 << (shift - floorLog2(height)));
  }
  else
  {
    deltaMvVerX = -deltaMvHorY;
    deltaMvVerY = deltaMvHorX;
  }

  const int mvScaleHor = affLT.getHor() * (1 << shift);
  const int mvScaleVer = affLT.getVer() * (1 << shift);

  int       blockWidth  = AFFINE_SUBBLOCK_SIZE;
  int       blockHeight = AFFINE_SUBBLOCK_SIZE;
  const int halfBW = blockWidth >> 1;
  const int halfBH = blockHeight >> 1;

  MotionBuf mb = pu.getMotionBuf();
  int mvScaleTmpHor, mvScaleTmpVer;
  const bool subblkMVSpreadOverLimit = InterPrediction::isSubblockVectorSpreadOverLimit( deltaMvHorX, deltaMvHorY, deltaMvVerX, deltaMvVerY, pu.interDir );

  for ( int h = 0; h < pu.Y().height; h += blockHeight )
  {
    for ( int w = 0; w < pu.Y().width; w += blockWidth )
    {
      if ( !subblkMVSpreadOverLimit )
      {
        mvScaleTmpHor = mvScaleHor + deltaMvHorX * (halfBW + w) + deltaMvVerX * (halfBH + h);
        mvScaleTmpVer = mvScaleVer + deltaMvHorY * (halfBW + w) + deltaMvVerY * (halfBH + h);
      }
      else
      {
        mvScaleTmpHor = mvScaleHor + deltaMvHorX * ( pu.Y().width >> 1 ) + deltaMvVerX * ( pu.Y().height >> 1 );
        mvScaleTmpVer = mvScaleVer + deltaMvHorY * ( pu.Y().width >> 1 ) + deltaMvVerY * ( pu.Y().height >> 1 );
      }
      Mv curMv(mvScaleTmpHor, mvScaleTmpVer);
      curMv.roundAffine(shift);
      curMv.clipToStorageBitDepth();

      for ( int y = (h >> MIN_CU_LOG2); y < ((h + blockHeight) >> MIN_CU_LOG2); y++ )
      {
        for ( int x = (w >> MIN_CU_LOG2); x < ((w + blockWidth) >> MIN_CU_LOG2); x++ )
        {
          mb.at(x, y).mv[eRefList] = curMv;
//#if DATA_DIST_COST
//          if (pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1 && rdo)
//          {
//            std::string path = "E:\\cloudGame\\data\\dist_cost\\lmcs_off\\mv\\1du\\640_128_128_128_1\\inherited\\affine_" + std::to_string(affLT.hor) + "_" + std::to_string(affLT.ver) + "_" + std::to_string(affRT.hor) + "_" + std::to_string(affRT.ver) + "_" + std::to_string(affLB.hor) + "_" + std::to_string(affLB.ver) + "_" + std::to_string(rdo) + ".txt";
//            std::ofstream ofs(path, std::ios::app);
//            ofs << x << "\t" << y << "\t" << curMv.hor << "\t" << curMv.ver << std::endl;
//          }
//#endif
        }
      }
    }
  }
//#if DATA_DIST_COST
//  if (pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1 && rdo)
//  {
//    std::string path = "E:\\cloudGame\\data\\dist_cost\\lmcs_off\\mv\\1du\\640_128_128_128_1\\inherited\\affine_" + std::to_string(affLT.hor) + "_" + std::to_string(affLT.ver) + "_" + std::to_string(affRT.hor) + "_" + std::to_string(affRT.ver) + "_" + std::to_string(affLB.hor) + "_" + std::to_string(affLB.ver) + "_" + std::to_string(rdo) + ".txt";
//    std::ofstream ofs(path, std::ios::app);
//    ofs << "===========================" << std::endl;
//  }
//#endif
  pu.mvAffi[eRefList][0] = affLT;
  pu.mvAffi[eRefList][1] = affRT;
  pu.mvAffi[eRefList][2] = affLB;
}

#if POSE
void PU::setAllposeMvField(PredictionUnit& pu, MvField* mvField, RefPicList eRefList
#if DATA_DIST_COST
  , int rdo
#endif
)
{
  // Set Mv
  Mv mv[3];
  for (int i = 0; i < 3; i++)
  {
    mv[i] = mvField[i].mv;
  }

  int16_t refidx = mvField[0].refIdx;   //参考图片索引
#if CTRL_COMB
  if (pu.cu->poseType == 0) refidx = mvField[1].refIdx;
#endif

#if IDEAL_SITU2
  if (1 /*pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1*/)
  {
#if DATA_DIST_COST
    setAllposeMvIdeal(pu, mv[0], mv[1], mv[2], refidx, eRefList, false, rdo);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#else
    setAllposeMvIdeal(pu, mv[0], mv[1], mv[2], refidx, eRefList);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#endif
  }
  else
  {
#if DATA_DIST_COST
    setAllposeMv(pu, mv[0], mv[1], mv[2], refidx, eRefList, false, rdo);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#else
    setAllposeMv(pu, mv[0], mv[1], mv[2], refidx, eRefList);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#endif
  }
#else
#if DATA_DIST_COST
  setAllposeMv(pu, mv[0], mv[1], mv[2], refidx, eRefList, false, rdo);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#else
  setAllposeMv(pu, mv[0], mv[1], mv[2], refidx, eRefList);   //CY 根据单向的三个控制点的MV，计算块内各个位置的MV
#endif
#endif

#if CTRL_COMB
  if (pu.cu->poseType == 0)
  {
    CHECK(mvField[1].refIdx != mvField[2].refIdx, "Pose mv corners don't have the same refIdx.");
  }
  else if (pu.cu->poseType == 1)
  {
    CHECK(mvField[0].refIdx != mvField[2].refIdx, "Pose mv corners don't have the same refIdx.");
  }
  else
  {
    CHECK(mvField[0].refIdx != mvField[1].refIdx, "Pose mv corners don't have the same refIdx.");
  }
  pu.refIdx[eRefList] = refidx;
#else
  // Set RefIdx
  CHECK(mvField[0].refIdx != mvField[1].refIdx || mvField[0].refIdx != mvField[2].refIdx,
        "Affine mv corners don't have the same refIdx.");
  pu.refIdx[eRefList] = mvField[0].refIdx;
#endif
}

#if IDEAL_SITU2
void PU::setAllposeMvIdeal(PredictionUnit& pu, Mv affLT, Mv affRT, Mv affLB, int16_t refidx, RefPicList eRefList, bool clipCPMVs
#if DATA_DIST_COST
  , int rdo
#endif
)
{
  MotionBuf mb = pu.getMotionBuf();

  double Ox = (pu.cs->picture->lwidth() - 1) / 2.0;
  double Oy = (pu.cs->picture->lheight() - 1) / 2.0;
  int x = pu.cs->area.lx(), y = pu.cs->area.ly();
  int       blockWidth = AFFINE_SUBBLOCK_SIZE;
  int       blockHeight = AFFINE_SUBBLOCK_SIZE;
  const int halfBW = blockWidth >> 1;
  const int halfBH = blockHeight >> 1;
  {
    for (int h = 0; h < pu.Y().height; h+= blockHeight)   //遍历pu
    {
      for (int w = 0; w < pu.Y().width; w+=blockWidth)
      {
        int x1 = w + x + halfBW;
        int y1 = h + y + halfBH;

        //以中心为原点的坐标
        double x11 = x1 - Ox;
        double y11 = y1 - Oy;

        int refPoc = pu.cs->slice->getRefPOC(eRefList, refidx);
        int theta_diff_degree = pu.cs->slice->getPOC() - refPoc;
        double theta_diff = degress2rad(theta_diff_degree);
        double x1_ref = (x11 * cos(theta_diff) - y11 * sin(theta_diff)) * 16;
        double y1_ref = (x11 * sin(theta_diff) + y11 * cos(theta_diff)) * 16;

        Mv curMv = Mv(x1_ref - (x11 * 16), y1_ref - (y11 * 16));
        mb.at(w/blockWidth, h/blockHeight).mv[eRefList] = curMv;

#if DATA_DIST_COST
        if (pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1 && rdo)
        {
          std::string path = "E:\\cloudGame\\data\\dist_cost\\lmcs_off\\mv\\1du\\640_128_128_128_1\\inherited\\pose_ideal_everypoint.txt";
          std::ofstream ofs(path, std::ios::app);
          ofs << w/blockWidth << "\t" << h/blockHeight << "\t" << curMv.hor << "\t" << curMv.ver << std::endl;
        }
#endif
      }
    }
  }
  pu.mvAffi[eRefList][0] = affLT;
  pu.mvAffi[eRefList][1] = affRT;
  pu.mvAffi[eRefList][2] = affLB;
}
#endif

void PU::setAllposeMv(PredictionUnit &pu, Mv affLT, Mv affRT, Mv affLB, int16_t refidx, RefPicList eRefList, bool clipCPMVs
#if DATA_DIST_COST
  , int rdo
#endif
) 
{
  int width  = pu.Y().width;
  //int shift = MAX_CU_DEPTH;
  const Slice &slice  = *pu.cs->slice;
  const Picture *const prefPic = slice.getRefPic(eRefList, refidx);  //得到参考图片 
  if (clipCPMVs)
  {
    affLT.mvCliptoStorageBitDepth();
    affRT.mvCliptoStorageBitDepth();
    if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      affLB.mvCliptoStorageBitDepth();
    }
  }

  int height  = pu.Y().height;

  //const int mvScaleHor  = affLT.getHor() * (1 << shift);
  //const int mvScaleVer  = affLT.getVer() * (1 << shift);
  int       blockWidth  = AFFINE_SUBBLOCK_SIZE;
  int       blockHeight = AFFINE_SUBBLOCK_SIZE;
  const int halfBW      = blockWidth >> 1;
  const int halfBH      = blockHeight >> 1;
  /*计算当前两个点和前帧的两个匹配点*/
    //double f1 = -1.1106, f2 = 1.1106; //ljy
  double f1 = -1.732, f2 = 1.732;
  double u1, v1, u2, v2, u3, v3, u4, v4;
  //u1             = double(pu.Y().topLeft().x);
  //v1             = double(pu.Y().topLeft().y);
  //u3             = double(pu.Y().topRight().x);
  //v3             = double(pu.Y().topRight().y);   //当前块的左上、右上控制点 选右上的话：参数的除数都为0
#if CTRL_COMB
  EPoseModel curPoseType = (EPoseModel)pu.cu->poseType;
  if (curPoseType == POSE_RT_LB)
  {
    u1 = double(pu.Y().topRight().x);//ljy
    v1 = double(pu.Y().topRight().y);   // puNeighbour的右上控制点

    u3 = double(pu.Y().bottomLeft().x);
    v3 = double(pu.Y().bottomLeft().y);   //左下控制点

    u2 = double(u1 + double(affRT.getHor()) / 16);
    v2 = double(v1 + double(affRT.getVer()) / 16);
    u4 = double(u3 + double(affLB.getHor()) / 16);
    v4 = double(v3 + double(affLB.getVer()) / 16);
  }
  else if (curPoseType == POSE_LT_LB)
  {
    u1 = double(pu.Y().x);//ljy
    v1 = double(pu.Y().y);   // puNeighbour的左上控制点

    u3 = double(pu.Y().bottomLeft().x);
    v3 = double(pu.Y().bottomLeft().y);   //左下控制点

    u2 = double(u1 + double(affLT.getHor()) / 16);
    v2 = double(v1 + double(affLT.getVer()) / 16);
    u4 = double(u3 + double(affLB.getHor()) / 16);
    v4 = double(v3 + double(affLB.getVer()) / 16);
  }
  else//POSE_LT_RT
  {
    u1 = double(pu.Y().x);//ljy
    v1 = double(pu.Y().y);   // puNeighbour的左上控制点

    u3 = double(pu.Y().topRight().x);
    v3 = double(pu.Y().topRight().y);   //右上控制点

    u2 = double(u1 + double(affLT.getHor()) / 16);
    v2 = double(v1 + double(affLT.getVer()) / 16);
    u4 = double(u3 + double(affRT.getHor()) / 16);
    v4 = double(v3 + double(affRT.getVer()) / 16);
  }
#else
  u1 = double(pu.Y().topRight().x);
  v1 = double(pu.Y().topRight().y);      //右上控制点

  u3 = double(pu.Y().bottomLeft().x);
  v3 = double(pu.Y().bottomLeft().y);    //左下控制点

  u2             = double(u1 + double(affRT.getHor()) / 16);
  v2             = double(v1 + double(affRT.getVer()) / 16);
  u4             = double(u3 + double(affLB.getHor()) / 16);
  v4             = double(v3 + double(affLB.getVer()) / 16);
#endif

  //归一化
  u1 = (u1 / pu.cs->picture->lwidth()) * 2 - 1;
  v1 = (v1 / pu.cs->picture->lwidth()) * 2 - 1;
  u3 = (u3 / pu.cs->picture->lwidth()) * 2 - 1;
  v3 = (v3 / pu.cs->picture->lwidth()) * 2 - 1;
  u2 = (u2 / pu.cs->picture->lwidth()) * 2 - 1;
  v2 = (v2 / pu.cs->picture->lwidth()) * 2 - 1;
  u4 = (u4 / pu.cs->picture->lwidth()) * 2 - 1;
  v4 = (v4 / pu.cs->picture->lwidth()) * 2 - 1;
  /*根据匹配点计算中间过程参数*/
  double E1, H1, I1, M1, E3, H3, I3, M3;
  double AA, P1A, P2A, P3A, P4A, Q1A, Q2A, Q3A, Q4A; //当前pic存的参数；
  double AB, P1B, P2B, P3B, P4B, Q1B, Q2B, Q3B, Q4B; //前一帧pic存的参数
  //当前帧的参数
  AA  = pu.cs->picture->AA;
  P1A = pu.cs->picture->P1A;
  P2A = pu.cs->picture->P2A;
  P3A = pu.cs->picture->P3A;
  P4A = pu.cs->picture->P4A;
  Q1A = pu.cs->picture->Q1A;
  Q2A = pu.cs->picture->Q2A;
  Q3A = pu.cs->picture->Q3A;
  Q4A = pu.cs->picture->Q4A;
  //前一帧的参数
  AB = prefPic->AA;
  P1B = prefPic->P1A;
  P2B = prefPic->P2A;
  P3B = prefPic->P3A;
  P4B = prefPic->P4A;
  Q1B = prefPic->Q1A;
  Q2B = prefPic->Q2A;
  Q3B = prefPic->Q3A;
  Q4B = prefPic->Q4A;

  // CY 这是按照Unity中取-f1+f2+1的公式形式
  E1 = ((P1A / P1B) * u1 - f1 * v1 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点1、2的参数theta_u的参数
  H1 = (f1 * v2 * P4B / (f2 * P1B)) - f1 * P3B / P1B;                      //配对点1、2的常数H

  I1 = (Q4A / Q4B) * v1 - f2 * u1 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;   //配对点1、2的参数theta_v的参数   same v1总是等于v3的
  M1 = (Q3B * f2 * u2 / (f1 * Q4B)) + f2 * Q1B / Q4B;   //配对点1、2的常数M

  E3 = ((P1A / P1B) * u3 - f1 * v3 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点3、4的参数theta_u的参数
  H3 = (f1 * v4 * P4B / (f2 * P1B)) - f1 * P3B / P1B;                      //配对点3、4的常数H

  I3 = (Q4A / Q4B) * v3 - f2 * u3 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;   //配对点3、4的参数theta_v的参数   same
  M3 = (Q3B * f2 * u4 / (f1 * Q4B)) + f2 * Q1B / Q4B;                    //配对点3、4的常数M

  // CY 这是按照The Lab中取-f1+f2-1的公式形式
  //E1 = ((P1A / P1B) * u1 - f1 * v1 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点1、2的参数theta_u的参数
  //H1 = (f1 * v2 * P4B / (f2 * P1B)) + f1 * P3B / P1B;                      //配对点1、2的常数H

  //I1 = (Q4A / Q4B) * v1 - f2 * u1 * Q3A / (f1 * Q4B)
  //     + f2 * Q1A / Q4B;   //配对点1、2的参数theta_v的参数   same v1总是等于v3的
  //M1 = (Q3B * f2 * u2 / (f1 * Q4B)) - f2 * Q1B / Q4B;   //配对点1、2的常数M

  //E3 = ((P1A / P1B) * u3 - f1 * v3 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点3、4的参数theta_u的参数
  //H3 = (f1 * v4 * P4B / (f2 * P1B)) + f1 * P3B / P1B;                      //配对点3、4的常数H

  //I3 = (Q4A / Q4B) * v3 - f2 * u3 * Q3A / (f1 * Q4B) + f2 * Q1A / Q4B;   //配对点3、4的参数theta_v的参数   same
  //M3 = (Q3B * f2 * u4 / (f1 * Q4B)) - f2 * Q1B / Q4B;                    //配对点3、4的常数M

#if THETA_UV
  double theta_u, theta_v, S, J, theta;   //最终的参数
  theta_u = -(E3 * v2 - E3 * v4 - I3 * u2 + I3 * u4 + H1 * I3 - H3 * I3 - E3 * M1 + E3 * M3) / (E1 * I3 - E3 * I1);
  S = (E3 * H1 * I1 - E1 * H3 * I3 - E1 * E3 * M1 + E1 * E3 * M3 + E1 * E3 * v2 - E1 * E3 * v4 - E3 * I1 * u2 + E1 * I3 * u4) / (E1 * I3 - E3 * I1);
  theta_v = -(E1 * v2 - E1 * v4 - I1 * u2 + I1 * u4 + H1 * I1 - H3 * I1 - E1 * M1 + E1 * M3) / (E1 * I3 - E3 * I1);
  J = (H1 * I1 * I3 - H3 * I1 * I3 - E1 * I3 * M1 + E3 * I1 * M3 + E1 * I3 * v2 - E3 * I1 * v4 - I1 * I3 * u2 + I1 * I3 * u4) / (E1 * I3 - E3 * I1);
#else
  double theta_u, S, theta_v, J;   //最终的参数
  theta_u = (u2 - u4 - H1 + H3) / (E1 - E3);
  S       = -(u2 * E3 - u4 * E1 + E1 * H3 - E3 * H1) / (E1 - E3);
  theta_v = (v2 - v4 - M1 + M3) / (I1 - I3);
  J       = -(v2 * I3 - v4 * I1 + I1 * M3 - I3 * M1) / (I1 - I3);
#endif

  MotionBuf mb = pu.getMotionBuf();

  double    u5, v5, u6, v6, E5, I5, A, u61, v61;
  double    detalx = 0, detaly = 0;   // double类型的水平和垂直MV
  int       detalx_int = 0, detaly_int = 0;  //

  for ( int h = 0; h < pu.Y().height; h += blockHeight )  //4*4存一个MV到motionbuf
  {
    for (int w = 0; w < pu.Y().width; w += blockWidth)
    {
      u5  = double(halfBW + w + pu.Y().topLeft().x) / pu.cs->picture->lwidth() * 2 - 1;             // u5
      v5  = double(halfBH + h + pu.Y().topLeft().y) / pu.cs->picture->lwidth() * 2 - 1;             // v5

      // CY 这是按照Unity中取-f1+f2+1的公式形式
      E5  = ((P1A / P1B) * u5 - f1 * v5 * P4A / (f2 * P1B) + f1 * P3A / P1B);   //配对点1的参数A的参数
      I5  = (Q4A / Q4B) * v5 - f2 * u5 * Q3A / (f1 * Q4B) - f2 * Q1A / Q4B;     //配对点1的参数A的参数

      // CY 这是按照The Lab中取-f1+f2-1的公式形式
      //E5  = ((P1A / P1B) * u5 - f1 * v5 * P4A / (f2 * P1B) - f1 * P3A / P1B);   //配对点1的参数A的参数
      //I5  = (Q4A / Q4B) * v5 - f2 * u5 * Q3A / (f1 * Q4B) + f2 * Q1A / Q4B;     //配对点1的参数A的参数

      A   = 1 / (1 - (P4B * Q3B) / (P1B * Q4B));

#if THETA_UV
      double q = sqrt((u5 - u1) * (u5 - u1) + (v5 - v1) * (v5 - v1)) / sqrt((u3 - u1) * (u3 - u1) + (v3 - v1) * (v3 - v1));
      theta = theta_u + q * (theta_v - theta_u);
      // CY 这是按照Unity中取-f1+f2+1的公式形式
      u61 = A
        * (theta * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S - f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
          + (f1 * P4B * Q1B) / (P1B * Q4B));
      v61 = A
        * (theta * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) + f2 * Q1B / Q4B
          - (Q3B * f2 * P3B) / (Q4B * P1B));
      // CY 这是按照The Lab中取-f1+f2-1的公式形式
    /*  u61 = A
            * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S + f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
               - (f1 * P4B * Q1B) / (P1B * Q4B));
      v61 = A
            * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) - f2 * Q1B / Q4B
               + (Q3B * f2 * P3B) / (Q4B * P1B));*/
#else
      // CY 这是按照Unity中取-f1+f2+1的公式形式
      u61 = A
            * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S - f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
               + (f1 * P4B * Q1B) / (P1B * Q4B));
      v61 = A
            * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) + f2 * Q1B / Q4B
               - (Q3B * f2 * P3B) / (Q4B * P1B));

      // CY 这是按照The Lab中取-f1+f2-1的公式形式
      //u61 = A
      //      * (theta_u * (E5 + (I5 * P4B * f1 / (f2 * P1B))) + S + f1 * P3B / P1B + (f1 * P4B * J) / (f2 * P1B)
      //         - (f1 * P4B * Q1B) / (P1B * Q4B));
      //v61 = A
      //      * (theta_v * (I5 + (Q3B * f2 * E5 / (Q4B * f1))) + J + (Q3B * f2 * S) / (Q4B * f1) - f2 * Q1B / Q4B
      //         + (Q3B * f2 * P3B) / (Q4B * P1B));
#endif

      /*归一化坐标转化成像素坐标*/

      u5 = (u5 + 1) * pu.cs->picture->lwidth()/2;
      v5 = (v5 + 1) * pu.cs->picture->lwidth()/2;
      if (isnan(u61))
      {
        u6     = 0;
        detalx = 0;
      }
      else
      {
        u6     = (u61 + 1) * pu.cs->picture->lwidth()/2;   //归一化转像素坐标
        detalx = (u6 - u5) * 16;    //转成1/16精度的MV存起来
      }
      if (isnan(v61))
      {
        v6     = 0;
        detaly = 0;
      }
      else
      {
        v6     = (v61 + 1) * pu.cs->picture->lwidth()/2;
        detaly = (v6 - v5) * 16;
      }

#if INT2ROUND
      detalx_int = round(detalx);
      detaly_int = round(detaly);
#else
      detalx_int = int(detalx);
      detaly_int = int(detaly);
#endif
      //if (detalx_int !=0 && detaly_int != 0) 
      //{
      //  Mv a(0, 0);
      //}

      Mv curMv (detalx_int,detaly_int);
      //curMv.roundAffine(shift);
      curMv.clipToStorageBitDepth();

      for ( int y = (h >> MIN_CU_LOG2); y < ((h + blockHeight) >> MIN_CU_LOG2); y++ )  //4*4的块存一个MV
      {
        for ( int x = (w >> MIN_CU_LOG2); x < ((w + blockWidth) >> MIN_CU_LOG2); x++ )
        {
          mb.at(x, y).mv[eRefList] = curMv;
#if DATA_DIST_COST
          if (pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1 && rdo)
          {
            std::string path = "E:\\cloudGame\\data\\dist_cost\\lmcs_off\\mv\\1du\\640_128_128_128_1\\inherited\\pose_ideal1_"+std::to_string(affLT.hor)+ "_"+ std::to_string(affLT.ver)+"_" + std::to_string(affRT.hor) + "_" + std::to_string(affRT.ver)+"_" + std::to_string(affLB.hor) + "_" + std::to_string(affLB.ver)+"_"+std::to_string(rdo)+"_round.txt";
            std::ofstream ofs(path, std::ios::app);
            ofs << x << "\t" << y << "\t" << curMv.hor << "\t" << curMv.ver << std::endl;
          }
#endif
        }
      }
    }
  } 
//#if DATA_DIST_COST
//  if (pu.cs->area.lx() == 640 && pu.cs->area.ly() == 128 && pu.cs->area.lwidth() == 128 && pu.cs->area.lheight() == 128 && pu.cs->slice->getPOC() == 1 && rdo)
//  {
//    std::string path = "E:\\cloudGame\\data\\dist_cost\\lmcs_off\\mv\\1du\\640_128_128_128_1\\inherited\\pose_" + std::to_string(affLT.hor) + "_" + std::to_string(affLT.ver) + "_" + std::to_string(affRT.hor) + "_" + std::to_string(affRT.ver) + "_" + std::to_string(affLB.hor) + "_" + std::to_string(affLB.ver) + "_" + std::to_string(rdo) + ".txt";
//    std::ofstream ofs(path, std::ios::app);
//    ofs << "===========================" << std::endl;
//  }
//#endif

  pu.mvAffi[eRefList][0] = affLT;
  pu.mvAffi[eRefList][1] = affRT;
  pu.mvAffi[eRefList][2] = affLB;
}
#endif

void clipColPos(int& posX, int& posY, const PredictionUnit& pu)
{
  Position puPos = pu.lumaPos();
  int log2CtuSize = floorLog2(pu.cs->sps->getCTUSize());
  int ctuX = ((puPos.x >> log2CtuSize) << log2CtuSize);
  int ctuY = ((puPos.y >> log2CtuSize) << log2CtuSize);
  int horMax;
  const SubPic &curSubPic = pu.cu->slice->getPPS()->getSubPicFromPos(puPos);
  if (curSubPic.getTreatedAsPicFlag())
  {
    horMax = std::min((int)curSubPic.getSubPicRight(), ctuX + (int)pu.cs->sps->getCTUSize() + 3);
  }
  else
  {
    horMax = std::min((int)pu.cs->pps->getPicWidthInLumaSamples() - 1, ctuX + (int)pu.cs->sps->getCTUSize() + 3);
  }
  int horMin = std::max((int)0, ctuX);
  int verMax = std::min( (int)pu.cs->pps->getPicHeightInLumaSamples() - 1, ctuY + (int)pu.cs->sps->getCTUSize() - 1 );
  int verMin = std::max((int)0, ctuY);

  posX = std::min(horMax, std::max(horMin, posX));
  posY = std::min(verMax, std::max(verMin, posY));
}

bool PU::getInterMergeSubPuMvpCand(const PredictionUnit &pu, MergeCtx &mrgCtx, const int count, int mmvdList)
{
  const Slice   &slice = *pu.cs->slice;
  const unsigned scale = 4 * std::max<int>(1, 4 * AMVP_DECIMATION_FACTOR / 4);
  const unsigned mask = ~(scale - 1);

  const Picture *pColPic = slice.getRefPic(RefPicList(slice.isInterB() ? 1 - slice.getColFromL0Flag() : 0), slice.getColRefIdx());
  Mv cTMv;

#if GDR_ENABLED
  const CodingStructure& cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
  bool isSubPuSolid[2] = { true, true };
#endif
  if ( count )
  {
    if ( (mrgCtx.interDirNeighbours[0] & (1 << REF_PIC_LIST_0)) && slice.getRefPic( REF_PIC_LIST_0, mrgCtx.mvFieldNeighbours[REF_PIC_LIST_0].refIdx ) == pColPic )
    {
      cTMv = mrgCtx.mvFieldNeighbours[REF_PIC_LIST_0].mv;
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        isSubPuSolid[REF_PIC_LIST_0] = mrgCtx.mvSolid[REF_PIC_LIST_0];
      }
#endif
    }
    else if ( slice.isInterB() && (mrgCtx.interDirNeighbours[0] & (1 << REF_PIC_LIST_1)) && slice.getRefPic( REF_PIC_LIST_1, mrgCtx.mvFieldNeighbours[REF_PIC_LIST_1].refIdx ) == pColPic )
    {
      cTMv = mrgCtx.mvFieldNeighbours[REF_PIC_LIST_1].mv;
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        isSubPuSolid[REF_PIC_LIST_1] = mrgCtx.mvSolid[REF_PIC_LIST_1];
      }
#endif
    }
  }

  ///////////////////////////////////////////////////////////////////////
  ////////          GET Initial Temporal Vector                  ////////
  ///////////////////////////////////////////////////////////////////////

  Mv cTempVector = cTMv;

  // compute the location of the current PU
  Position puPos = pu.lumaPos();
  Size puSize = pu.lumaSize();
  int numPartLine = std::max(puSize.width >> ATMVP_SUB_BLOCK_SIZE, 1u);
  int numPartCol = std::max(puSize.height >> ATMVP_SUB_BLOCK_SIZE, 1u);
  int puHeight = numPartCol == 1 ? puSize.height : 1 << ATMVP_SUB_BLOCK_SIZE;
  int puWidth = numPartLine == 1 ? puSize.width : 1 << ATMVP_SUB_BLOCK_SIZE;

  Mv cColMv;
  int refIdx = 0;
  // use coldir.
  const bool isBSlice = slice.isInterB();

  Position centerPos;

  bool found = false;
  cTempVector = cTMv;

  cTempVector.changePrecision(MV_PRECISION_SIXTEENTH, MV_PRECISION_INT);
  int tempX = cTempVector.getHor();
  int tempY = cTempVector.getVer();

  centerPos.x = puPos.x + (puSize.width >> 1) + tempX;
  centerPos.y = puPos.y + (puSize.height >> 1) + tempY;

  clipColPos(centerPos.x, centerPos.y, pu);

  centerPos = Position{ PosType(centerPos.x & mask), PosType(centerPos.y & mask) };

  // derivation of center motion parameters from the collocated CU
  const MotionInfo &mi = pColPic->cs->getMotionInfo(centerPos);

  if (mi.isInter && mi.isIBCmot == false)
  {
    mrgCtx.interDirNeighbours[count] = 0;

    for (unsigned currRefListId = 0; currRefListId < (isBSlice ? 2 : 1); currRefListId++)
    {
      RefPicList  currRefPicList = RefPicList(currRefListId);

      if (getColocatedMVP(pu, currRefPicList, centerPos, cColMv, refIdx, true))
      {
        // set as default, for further motion vector field spanning
        mrgCtx.mvFieldNeighbours[(count << 1) + currRefListId].setMvField(cColMv, 0);
        mrgCtx.interDirNeighbours[count] |= (1 << currRefListId);
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          mrgCtx.mvSolid[(count << 1) + currRefListId] = cs.isClean(centerPos, currRefPicList, refIdx);
        }
#endif
        mrgCtx.bcwIdx[count] = BCW_DEFAULT;
        found = true;
      }
      else
      {
        mrgCtx.mvFieldNeighbours[(count << 1) + currRefListId].setMvField(Mv(), NOT_VALID);
        mrgCtx.interDirNeighbours[count] &= ~(1 << currRefListId);
      }
    }
  }

  if (!found)
  {
    return false;
  }
  if (mmvdList != 1)
  {
    int xOff = (puWidth >> 1) + tempX;
    int yOff = (puHeight >> 1) + tempY;

    MotionBuf &mb = mrgCtx.subPuMvpMiBuf;

    const bool isBiPred = isBipredRestriction(pu);

    for (int y = puPos.y; y < puPos.y + puSize.height; y += puHeight)
    {
      for (int x = puPos.x; x < puPos.x + puSize.width; x += puWidth)
      {
        Position colPos{ x + xOff, y + yOff };

        clipColPos(colPos.x, colPos.y, pu);

        colPos = Position{ PosType(colPos.x & mask), PosType(colPos.y & mask) };

        const MotionInfo &colMi = pColPic->cs->getMotionInfo(colPos);

        MotionInfo mi;

        found       = false;
        mi.isInter  = true;
        mi.sliceIdx = slice.getIndependentSliceIdx();
        mi.isIBCmot = false;
        if (colMi.isInter && colMi.isIBCmot == false)
        {
          for (unsigned currRefListId = 0; currRefListId < (isBSlice ? 2 : 1); currRefListId++)
          {
            RefPicList currRefPicList = RefPicList(currRefListId);
            if (getColocatedMVP(pu, currRefPicList, colPos, cColMv, refIdx, true))
            {
              mi.refIdx[currRefListId] = 0;
              mi.mv[currRefListId]     = cColMv;
              found                    = true;
#if GDR_ENABLED
              if (isEncodeGdrClean)
              {
                isSubPuSolid[currRefPicList] = isSubPuSolid[currRefPicList] && cs.isClean(colPos, currRefPicList, refIdx);
              }
#endif
            }
          }
        }
        if (!found)
        {
          mi.mv[0]     = mrgCtx.mvFieldNeighbours[(count << 1) + 0].mv;
          mi.mv[1]     = mrgCtx.mvFieldNeighbours[(count << 1) + 1].mv;
          mi.refIdx[0] = mrgCtx.mvFieldNeighbours[(count << 1) + 0].refIdx;
          mi.refIdx[1] = mrgCtx.mvFieldNeighbours[(count << 1) + 1].refIdx;
        }

        mi.interDir = (mi.refIdx[0] != -1 ? 1 : 0) + (mi.refIdx[1] != -1 ? 2 : 0);

        if (isBiPred && mi.interDir == 3)
        {
          mi.interDir  = 1;
          mi.mv[1]     = Mv();
          mi.refIdx[1] = NOT_VALID;
        }

        mb.subBuf(g_miScaling.scale(Position{ x, y } - pu.lumaPos()), g_miScaling.scale(Size(puWidth, puHeight)))
          .fill(mi);
      }
    }
  }

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    // the final if it is solid
    mrgCtx.mvSolid[(count << 1) + 0] = mrgCtx.mvSolid[(count << 1) + 0] && isSubPuSolid[0];
    mrgCtx.mvSolid[(count << 1) + 1] = mrgCtx.mvSolid[(count << 1) + 1] && isSubPuSolid[1];
  }
#endif

  return true;
}

void PU::spanMotionInfo( PredictionUnit &pu, const MergeCtx &mrgCtx )
{
  MotionBuf mb = pu.getMotionBuf();

  if (!pu.mergeFlag || pu.mergeType == MRG_TYPE_DEFAULT_N || pu.mergeType == MRG_TYPE_IBC)
  {
    MotionInfo mi;

    mi.isInter = !CU::isIntra(*pu.cu);
    mi.isIBCmot = CU::isIBC(*pu.cu);
    mi.sliceIdx = pu.cu->slice->getIndependentSliceIdx();

    if( mi.isInter )
    {
      mi.interDir = pu.interDir;
      mi.useAltHpelIf = pu.cu->imv == IMV_HPEL;

      for( int i = 0; i < NUM_REF_PIC_LIST_01; i++ )
      {
        mi.mv[i]     = pu.mv[i];
        mi.refIdx[i] = pu.refIdx[i];
      }
      if (mi.isIBCmot)
      {
        mi.bv = pu.bv;
      }
    }
#if POSE
    if (pu.cu->affine||pu.cu->poseFlag)
#else
    if( pu.cu->affine )
#endif
    {
      for( int y = 0; y < mb.height; y++ )
      {
        for( int x = 0; x < mb.width; x++ )
        {
          MotionInfo &dest = mb.at( x, y );
          dest.isInter  = mi.isInter;
          dest.isIBCmot = false;
          dest.interDir = mi.interDir;
          dest.sliceIdx = mi.sliceIdx;
          for( int i = 0; i < NUM_REF_PIC_LIST_01; i++ )
          {
            if( mi.refIdx[i] == -1 )
            {
              dest.mv[i] = Mv();
            }
            dest.refIdx[i] = mi.refIdx[i];
          }
        }
      }
    }
    else
    {
      mb.fill( mi );
    }
  }
  else if (pu.mergeType == MRG_TYPE_SUBPU_ATMVP)
  {
    CHECK(mrgCtx.subPuMvpMiBuf.area() == 0 || !mrgCtx.subPuMvpMiBuf.buf, "Buffer not initialized");
    mb.copyFrom(mrgCtx.subPuMvpMiBuf);
  }
  else
  {
    if( isBipredRestriction( pu ) )
    {
      for( int y = 0; y < mb.height; y++ )
      {
        for( int x = 0; x < mb.width; x++ )
        {
          MotionInfo &mi = mb.at( x, y );
          if( mi.interDir == 3 )
          {
            mi.interDir  = 1;
            mi.mv    [1] = Mv();
            mi.refIdx[1] = NOT_VALID;
          }
        }
      }
    }
  }
}

void PU::applyImv( PredictionUnit& pu, MergeCtx &mrgCtx, InterPrediction *interPred )
{
  if( !pu.mergeFlag )
  {
    if( pu.interDir != 2 /* PRED_L1 */ )
    {
      pu.mvd[0].changeTransPrecAmvr2Internal(pu.cu->imv);
      unsigned mvpIdx = pu.mvpIdx[0];
      AMVPInfo amvpInfo;
      if (CU::isIBC(*pu.cu))
      {
        PU::fillIBCMvpCand(pu, amvpInfo);
      }
      else
      PU::fillMvpCand(pu, REF_PIC_LIST_0, pu.refIdx[0], amvpInfo);
      pu.mvpNum[0] = amvpInfo.numCand;
      pu.mvpIdx[0] = mvpIdx;
      pu.mv[0]     = amvpInfo.mvCand[mvpIdx] + pu.mvd[0];
      pu.mv[0].mvCliptoStorageBitDepth();
    }

    if (pu.interDir != 1 /* PRED_L0 */)
    {
      if( !( pu.cu->cs->picHeader->getMvdL1ZeroFlag() && pu.interDir == 3 ) && pu.cu->imv )/* PRED_BI */
      {
        pu.mvd[1].changeTransPrecAmvr2Internal(pu.cu->imv);
      }
      unsigned mvpIdx = pu.mvpIdx[1];
      AMVPInfo amvpInfo;
      PU::fillMvpCand(pu, REF_PIC_LIST_1, pu.refIdx[1], amvpInfo);
      pu.mvpNum[1] = amvpInfo.numCand;
      pu.mvpIdx[1] = mvpIdx;
      pu.mv[1]     = amvpInfo.mvCand[mvpIdx] + pu.mvd[1];
      pu.mv[1].mvCliptoStorageBitDepth();
    }
  }
  else
  {
    // this function is never called for merge
    THROW("unexpected");
    PU::getInterMergeCandidates(pu, mrgCtx, 0);

    mrgCtx.setMergeInfo( pu, pu.mergeIdx );
  }

  PU::spanMotionInfo( pu, mrgCtx );
}

bool PU::isSimpleSymmetricBiPred(const PredictionUnit &pu)
{
  const int refIdx0 = pu.refIdx[REF_PIC_LIST_0];
  const int refIdx1 = pu.refIdx[REF_PIC_LIST_1];

  if (refIdx0 >= 0 && refIdx1 >= 0)
  {
    const Slice *slice = pu.cu->slice;

    if (slice->getRefPic(REF_PIC_LIST_0, refIdx0)->longTerm || slice->getRefPic(REF_PIC_LIST_1, refIdx1)->longTerm)
    {
      return false;
    }

    if (pu.cu->bcwIdx != BCW_DEFAULT)
    {
      return false;
    }

    if (WPScalingParam::isWeighted(slice->getWpScaling(REF_PIC_LIST_0, refIdx0))
        || WPScalingParam::isWeighted(slice->getWpScaling(REF_PIC_LIST_1, refIdx1)))
    {
      return false;
    }

    const int poc0 = slice->getRefPOC(REF_PIC_LIST_0, refIdx0);
    const int poc1 = slice->getRefPOC(REF_PIC_LIST_1, refIdx1);
    const int poc  = slice->getPOC();

    return poc - poc0 == poc1 - poc;
  }

  return false;
}

void PU::restrictBiPredMergeCandsOne(PredictionUnit &pu)
{
  if (PU::isBipredRestriction(pu))
  {
    if (pu.interDir == 3)
    {
      pu.interDir = 1;
      pu.refIdx[1] = -1;
      pu.mv[1] = Mv(0, 0);
      pu.cu->bcwIdx = BCW_DEFAULT;
    }
  }
}

void PU::getGeoMergeCandidates( const PredictionUnit &pu, MergeCtx& geoMrgCtx )
{
  MergeCtx tmpMergeCtx;

  const uint32_t maxNumMergeCand = pu.cs->sps->getMaxNumMergeCand();
  geoMrgCtx.numValidMergeCand = 0;

#if GDR_ENABLED
  CodingStructure &cs = *pu.cs;
  const bool isEncodeGdrClean = cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu.Y().topRight(), CHANNEL_TYPE_LUMA)) || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif
  for (int32_t i = 0; i < GEO_MAX_NUM_UNI_CANDS; i++)
  {
    geoMrgCtx.bcwIdx[i]                           = BCW_DEFAULT;
    geoMrgCtx.interDirNeighbours[i]               = 0;
    geoMrgCtx.mvFieldNeighbours[2 * i].refIdx     = NOT_VALID;
    geoMrgCtx.mvFieldNeighbours[2 * i + 1].refIdx = NOT_VALID;
    geoMrgCtx.mvFieldNeighbours[2 * i].mv         = Mv();
    geoMrgCtx.mvFieldNeighbours[2 * i + 1].mv     = Mv();
#if GDR_ENABLED
    if (isEncodeGdrClean)
    {
      geoMrgCtx.mvSolid[(i << 1) + 0] = true;
      geoMrgCtx.mvSolid[(i << 1) + 1] = true;
    }
#endif
    geoMrgCtx.useAltHpelIf[i] = false;
  }

  PU::getInterMergeCandidates(pu, tmpMergeCtx, 0);

  for (int32_t i = 0; i < maxNumMergeCand; i++)
  {
    int parity = i & 1;
    if( tmpMergeCtx.interDirNeighbours[i] & (0x01 + parity) )
    {
      geoMrgCtx.interDirNeighbours[geoMrgCtx.numValidMergeCand] = 1 + parity;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].mv = Mv(0, 0);
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].mv = tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].mv;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].refIdx = -1;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].refIdx = tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].refIdx;
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Mv  mv = tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].mv;
        int refIdx = tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].refIdx;
        RefPicList refPicList = parity ? REF_PIC_LIST_1 : REF_PIC_LIST_0;
        geoMrgCtx.mvSolid[(geoMrgCtx.numValidMergeCand << 1) + !parity] = true;
        geoMrgCtx.mvSolid[(geoMrgCtx.numValidMergeCand << 1) + parity] = tmpMergeCtx.mvSolid[(i << 1) + parity];
        geoMrgCtx.mvValid[(geoMrgCtx.numValidMergeCand << 1) + !parity] = true;
        geoMrgCtx.mvValid[(geoMrgCtx.numValidMergeCand << 1) + parity] = cs.isClean(pu.Y().bottomRight(), mv, refPicList, refIdx);
      }
#endif
      geoMrgCtx.numValidMergeCand++;
      if (geoMrgCtx.numValidMergeCand == GEO_MAX_NUM_UNI_CANDS)
      {
        return;
      }
      continue;
    }

    if (tmpMergeCtx.interDirNeighbours[i] & (0x02 - parity))
    {
      geoMrgCtx.interDirNeighbours[geoMrgCtx.numValidMergeCand] = 2 - parity;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].mv = tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].mv;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].mv = Mv(0, 0);
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].refIdx = tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].refIdx;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].refIdx = -1;
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        Mv  mv = tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].mv;
        int refIdx = tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].refIdx;
        RefPicList refPicList = (!parity) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;
        geoMrgCtx.mvSolid[(geoMrgCtx.numValidMergeCand << 1) + !parity] = tmpMergeCtx.mvSolid[(i << 1) + !parity];
        geoMrgCtx.mvSolid[(geoMrgCtx.numValidMergeCand << 1) + parity] = true;
        geoMrgCtx.mvValid[(geoMrgCtx.numValidMergeCand << 1) + !parity] = cs.isClean(pu.Y().bottomRight(), mv, refPicList, refIdx);
        geoMrgCtx.mvValid[(geoMrgCtx.numValidMergeCand << 1) + parity] = true;
      }
#endif
      geoMrgCtx.numValidMergeCand++;
      if (geoMrgCtx.numValidMergeCand == GEO_MAX_NUM_UNI_CANDS)
      {
        return;
      }
    }
  }
}

void PU::spanGeoMotionInfo(PredictionUnit &pu, const MergeCtx &geoMrgCtx, const uint8_t splitDir,
                           const uint8_t candIdx0, const uint8_t candIdx1)
{
  pu.geoSplitDir  = splitDir;
  pu.geoMergeIdx0 = candIdx0;
  pu.geoMergeIdx1 = candIdx1;
  MotionBuf mb = pu.getMotionBuf();

  MotionInfo biMv;
  biMv.isInter  = true;
  biMv.sliceIdx = pu.cs->slice->getIndependentSliceIdx();

  if( geoMrgCtx.interDirNeighbours[candIdx0] == 1 && geoMrgCtx.interDirNeighbours[candIdx1] == 2 )
  {
    biMv.interDir  = 3;
    biMv.mv[0]     = geoMrgCtx.mvFieldNeighbours[ candIdx0 << 1     ].mv;
    biMv.mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[ candIdx0 << 1     ].refIdx;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
  }
  else if( geoMrgCtx.interDirNeighbours[candIdx0] == 2 && geoMrgCtx.interDirNeighbours[candIdx1] == 1 )
  {
    biMv.interDir  = 3;
    biMv.mv[0]     = geoMrgCtx.mvFieldNeighbours[ candIdx1 << 1     ].mv;
    biMv.mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].mv;
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[ candIdx1 << 1     ].refIdx;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].refIdx;
  }
  else if( geoMrgCtx.interDirNeighbours[candIdx0] == 1 && geoMrgCtx.interDirNeighbours[candIdx1] == 1 )
  {
    biMv.interDir = 1;
    biMv.mv[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].mv;
    biMv.mv[1] = Mv(0, 0);
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].refIdx;
    biMv.refIdx[1] = -1;
  }
  else if( geoMrgCtx.interDirNeighbours[candIdx0] == 2 && geoMrgCtx.interDirNeighbours[candIdx1] == 2 )
  {
    biMv.interDir = 2;
    biMv.mv[0] = Mv(0, 0);
    biMv.mv[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
    biMv.refIdx[0] = -1;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
  }

  int16_t angle = g_GeoParams[splitDir][0];
  int tpmMask = 0;
  int lookUpY = 0, motionIdx = 0;
  bool isFlip = angle >= 13 && angle <= 27;
  int distanceIdx = g_GeoParams[splitDir][1];
  int distanceX = angle;
  int distanceY = (distanceX + (GEO_NUM_ANGLES >> 2)) % GEO_NUM_ANGLES;
  int offsetX = (-(int)pu.lwidth()) >> 1;
  int offsetY = (-(int)pu.lheight()) >> 1;
  if (distanceIdx > 0)
  {
    if (angle % 16 == 8 || (angle % 16 != 0 && pu.lheight() >= pu.lwidth()))
    {
      offsetY += angle < 16 ? ((distanceIdx * pu.lheight()) >> 3) : -(int)((distanceIdx * pu.lheight()) >> 3);
    }
    else
    {
      offsetX += angle < 16 ? ((distanceIdx * pu.lwidth()) >> 3) : -(int)((distanceIdx * pu.lwidth()) >> 3);
    }
  }
  for (int y = 0; y < mb.height; y++)
  {
    lookUpY = (2 * (4 * y + offsetY) + 5) * g_dis[distanceY];
    for (int x = 0; x < mb.width; x++)
    {
      motionIdx = (2 * (4 * x + offsetX) + 5) * g_dis[distanceX] + lookUpY;
      tpmMask = abs(motionIdx) < 32 ? 2 : (motionIdx <= 0 ? (1 - isFlip) : isFlip);
      if (tpmMask == 2)
      {
        mb.at(x, y).isInter = true;
        mb.at(x, y).interDir = biMv.interDir;
        mb.at(x, y).refIdx[0] = biMv.refIdx[0];
        mb.at(x, y).refIdx[1] = biMv.refIdx[1];
        mb.at(x, y).mv[0] = biMv.mv[0];
        mb.at(x, y).mv[1] = biMv.mv[1];
        mb.at(x, y).sliceIdx = biMv.sliceIdx;
      }
      else if (tpmMask == 0)
      {
        mb.at(x, y).isInter = true;
        mb.at(x, y).interDir = geoMrgCtx.interDirNeighbours[candIdx0];
        mb.at(x, y).refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].refIdx;
        mb.at(x, y).refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].refIdx;
        mb.at(x, y).mv[0] = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].mv;
        mb.at(x, y).mv[1] = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].mv;
        mb.at(x, y).sliceIdx = biMv.sliceIdx;
      }
      else
      {
        mb.at(x, y).isInter = true;
        mb.at(x, y).interDir = geoMrgCtx.interDirNeighbours[candIdx1];
        mb.at(x, y).refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].refIdx;
        mb.at(x, y).refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
        mb.at(x, y).mv[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].mv;
        mb.at(x, y).mv[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
        mb.at(x, y).sliceIdx = biMv.sliceIdx;
      }
    }
  }
}

void PU::getNeighborAffineInfo(const PredictionUnit& pu, int& numNeighborAvai, int& numNeighborAffine)
{
  const Position& posLT = pu.Y().topLeft();
  const Position& posRT = pu.Y().topRight();
  const Position& posLB = pu.Y().bottomLeft();
  const int neighborNum = 5;
  const PredictionUnit* neighbor[neighborNum];
  neighbor[0] = pu.cs->getPURestricted(posRT.offset(0, -1), pu, pu.chType); // above
  neighbor[1] = pu.cs->getPURestricted(posLB.offset(-1, 0), pu, pu.chType); // left
  neighbor[2] = pu.cs->getPURestricted(posRT.offset(1, -1), pu, pu.chType); // above-right
  neighbor[3] = pu.cs->getPURestricted(posLB.offset(-1, 1), pu, pu.chType); // left-bottom
  neighbor[4] = pu.cs->getPURestricted(posLT.offset(-1, -1), pu, pu.chType); // above-left
  numNeighborAvai = 0;
  numNeighborAffine = 0;
  for (int i = 0; i < neighborNum; i++)
  {
    if (neighbor[i] != nullptr)
    {
      numNeighborAvai++;
      numNeighborAffine += neighbor[i]->cu->affine;
    }
  }
}

bool CU::hasSubCUNonZeroMVd( const CodingUnit& cu )
{
  bool nonZeroMvd = false;

  for( const auto &pu : CU::traversePUs( cu ) )
  {
    if( ( !pu.mergeFlag ) && ( !cu.skip ) )
    {
      if( pu.interDir != 2 /* PRED_L1 */ )
      {
        nonZeroMvd |= pu.mvd[REF_PIC_LIST_0].getHor() != 0;
        nonZeroMvd |= pu.mvd[REF_PIC_LIST_0].getVer() != 0;
      }
      if( pu.interDir != 1 /* PRED_L0 */ )
      {
        if( !pu.cu->cs->picHeader->getMvdL1ZeroFlag() || pu.interDir != 3 /* PRED_BI */ )
        {
          nonZeroMvd |= pu.mvd[REF_PIC_LIST_1].getHor() != 0;
          nonZeroMvd |= pu.mvd[REF_PIC_LIST_1].getVer() != 0;
        }
      }
    }
  }

  return nonZeroMvd;
}

bool CU::hasSubCUNonZeroAffineMVd( const CodingUnit& cu )
{
  bool nonZeroAffineMvd = false;

  if ( !cu.affine || cu.firstPU->mergeFlag )
  {
    return false;
  }

  for ( const auto &pu : CU::traversePUs( cu ) )
  {
    if ( ( !pu.mergeFlag ) && ( !cu.skip ) )
    {
      if ( pu.interDir != 2 /* PRED_L1 */ )
      {
        for ( int i = 0; i < ( cu.affineType == AFFINEMODEL_6PARAM ? 3 : 2 ); i++ )
        {
          nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_0][i].getHor() != 0;
          nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_0][i].getVer() != 0;
        }
      }

      if ( pu.interDir != 1 /* PRED_L0 */ )
      {
        if ( !pu.cu->cs->picHeader->getMvdL1ZeroFlag() || pu.interDir != 3 /* PRED_BI */ )
        {
          for ( int i = 0; i < ( cu.affineType == AFFINEMODEL_6PARAM ? 3 : 2 ); i++ )
          {
            nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_1][i].getHor() != 0;
            nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_1][i].getVer() != 0;
          }
        }
      }
    }
  }

  return nonZeroAffineMvd;
}

uint8_t CU::getSbtInfo( uint8_t idx, uint8_t pos )
{
  return ( pos << 4 ) + ( idx << 0 );
}

uint8_t CU::getSbtIdx( const uint8_t sbtInfo )
{
  return ( sbtInfo >> 0 ) & 0xf;
}

uint8_t CU::getSbtPos( const uint8_t sbtInfo )
{
  return ( sbtInfo >> 4 ) & 0x3;
}

uint8_t CU::getSbtMode( uint8_t sbtIdx, uint8_t sbtPos )
{
  uint8_t sbtMode = 0;
  switch( sbtIdx )
  {
  case SBT_VER_HALF: sbtMode = sbtPos + SBT_VER_H0;  break;
  case SBT_HOR_HALF: sbtMode = sbtPos + SBT_HOR_H0;  break;
  case SBT_VER_QUAD: sbtMode = sbtPos + SBT_VER_Q0;  break;
  case SBT_HOR_QUAD: sbtMode = sbtPos + SBT_HOR_Q0;  break;
  default:           assert( 0 );
  }

  assert( sbtMode < NUMBER_SBT_MODE );
  return sbtMode;
}

uint8_t CU::getSbtIdxFromSbtMode( uint8_t sbtMode )
{
  if( sbtMode <= SBT_VER_H1 )
  {
    return SBT_VER_HALF;
  }
  else if( sbtMode <= SBT_HOR_H1 )
  {
    return SBT_HOR_HALF;
  }
  else if( sbtMode <= SBT_VER_Q1 )
  {
    return SBT_VER_QUAD;
  }
  else if( sbtMode <= SBT_HOR_Q1 )
  {
    return SBT_HOR_QUAD;
  }
  else
  {
    assert( 0 );
    return 0;
  }
}

uint8_t CU::getSbtPosFromSbtMode( uint8_t sbtMode )
{
  if( sbtMode <= SBT_VER_H1 )
  {
    return sbtMode - SBT_VER_H0;
  }
  else if( sbtMode <= SBT_HOR_H1 )
  {
    return sbtMode - SBT_HOR_H0;
  }
  else if( sbtMode <= SBT_VER_Q1 )
  {
    return sbtMode - SBT_VER_Q0;
  }
  else if( sbtMode <= SBT_HOR_Q1 )
  {
    return sbtMode - SBT_HOR_Q0;
  }
  else
  {
    assert( 0 );
    return 0;
  }
}

uint8_t CU::targetSbtAllowed( uint8_t sbtIdx, uint8_t sbtAllowed )
{
  uint8_t val = 0;
  switch( sbtIdx )
  {
  case SBT_VER_HALF: val = ( ( sbtAllowed >> SBT_VER_HALF ) & 0x1 ); break;
  case SBT_HOR_HALF: val = ( ( sbtAllowed >> SBT_HOR_HALF ) & 0x1 ); break;
  case SBT_VER_QUAD: val = ( ( sbtAllowed >> SBT_VER_QUAD ) & 0x1 ); break;
  case SBT_HOR_QUAD: val = ( ( sbtAllowed >> SBT_HOR_QUAD ) & 0x1 ); break;
  default: THROW("unknown SBT type");
  }
  return val;
}

uint8_t CU::numSbtModeRdo( uint8_t sbtAllowed )
{
  uint8_t num = 0;
  uint8_t sum = 0;
  num = targetSbtAllowed( SBT_VER_HALF, sbtAllowed ) + targetSbtAllowed( SBT_HOR_HALF, sbtAllowed );
  sum += std::min( SBT_NUM_RDO, ( num << 1 ) );
  num = targetSbtAllowed( SBT_VER_QUAD, sbtAllowed ) + targetSbtAllowed( SBT_HOR_QUAD, sbtAllowed );
  sum += std::min( SBT_NUM_RDO, ( num << 1 ) );
  return sum;
}

bool CU::isSbtMode( const uint8_t sbtInfo )
{
  const uint8_t sbtIdx = getSbtIdx(sbtInfo);
  return sbtIdx >= SBT_VER_HALF && sbtIdx <= SBT_HOR_QUAD;
}

bool CU::isSameSbtSize( const uint8_t sbtInfo1, const uint8_t sbtInfo2 )
{
  const uint8_t sbtIdx1 = getSbtIdxFromSbtMode(sbtInfo1);
  const uint8_t sbtIdx2 = getSbtIdxFromSbtMode(sbtInfo2);
  if( sbtIdx1 == SBT_HOR_HALF || sbtIdx1 == SBT_VER_HALF )
  {
    return sbtIdx2 == SBT_HOR_HALF || sbtIdx2 == SBT_VER_HALF;
  }
  else if( sbtIdx1 == SBT_HOR_QUAD || sbtIdx1 == SBT_VER_QUAD )
  {
    return sbtIdx2 == SBT_HOR_QUAD || sbtIdx2 == SBT_VER_QUAD;
  }
  else
  {
    return false;
  }
}

bool CU::isPredRegDiffFromTB(const CodingUnit &cu, const ComponentID compID)
{
  return (compID == COMPONENT_Y)
         && (cu.ispMode == VER_INTRA_SUBPARTITIONS
             && CU::isMinWidthPredEnabledForBlkSize(cu.blocks[compID].width, cu.blocks[compID].height));
}

bool CU::isMinWidthPredEnabledForBlkSize(const int w, const int h)
{
  return ((w == 8 && h > 4) || w == 4);
}

bool CU::isFirstTBInPredReg(const CodingUnit& cu, const ComponentID compID, const CompArea &area)
{
  return (compID == COMPONENT_Y) && cu.ispMode && ((area.topLeft().x - cu.Y().topLeft().x) % PRED_REG_MIN_WIDTH == 0);
}

void CU::adjustPredArea(CompArea &area)
{
  area.width = std::max<int>(PRED_REG_MIN_WIDTH, area.width);
}

bool CU::isBcwIdxCoded( const CodingUnit &cu )
{
  if( cu.cs->sps->getUseBcw() == false )
  {
    CHECK(cu.bcwIdx != BCW_DEFAULT, "Error: cu.bcwIdx != BCW_DEFAULT");
    return false;
  }

  if (CU::isIBC(cu))
  {
    return false;
  }

  if (CU::isIntra(cu) || cu.cs->slice->isInterP())
  {
    return false;
  }

  if( cu.lwidth() * cu.lheight() < BCW_SIZE_CONSTRAINT )
  {
    return false;
  }

  if( !cu.firstPU->mergeFlag )
  {
    if( cu.firstPU->interDir == 3 )
    {
      const int refIdx0 = cu.firstPU->refIdx[REF_PIC_LIST_0];
      const int refIdx1 = cu.firstPU->refIdx[REF_PIC_LIST_1];

      const WPScalingParam *wp0 = cu.cs->slice->getWpScaling(REF_PIC_LIST_0, refIdx0);
      const WPScalingParam *wp1 = cu.cs->slice->getWpScaling(REF_PIC_LIST_1, refIdx1);

      return !(WPScalingParam::isWeighted(wp0) || WPScalingParam::isWeighted(wp1));
    }
  }

  return false;
}

uint8_t CU::getValidBcwIdx( const CodingUnit &cu )
{
  if( cu.firstPU->interDir == 3 && !cu.firstPU->mergeFlag )
  {
    return cu.bcwIdx;
  }
  else if( cu.firstPU->interDir == 3 && cu.firstPU->mergeFlag && cu.firstPU->mergeType == MRG_TYPE_DEFAULT_N )
  {
    // This is intended to do nothing here.
  }
  else if( cu.firstPU->mergeFlag && cu.firstPU->mergeType == MRG_TYPE_SUBPU_ATMVP )
  {
    CHECK(cu.bcwIdx != BCW_DEFAULT, " cu.bcwIdx != BCW_DEFAULT ");
  }
  else
  {
    CHECK(cu.bcwIdx != BCW_DEFAULT, " cu.bcwIdx != BCW_DEFAULT ");
  }

  return BCW_DEFAULT;
}

bool CU::bdpcmAllowed( const CodingUnit& cu, const ComponentID compID )
{
  SizeType transformSkipMaxSize = 1 << cu.cs->sps->getLog2MaxTransformSkipBlockSize();

  bool bdpcmAllowed = cu.cs->sps->getBDPCMEnabledFlag() && CU::isIntra(cu);
  if (isLuma(compID))
  {
    bdpcmAllowed &= (cu.lwidth() <= transformSkipMaxSize && cu.lheight() <= transformSkipMaxSize);
  }
  else
  {
    bdpcmAllowed &= (cu.chromaSize().width <= transformSkipMaxSize && cu.chromaSize().height <= transformSkipMaxSize)
                    && !cu.colorTransform;
  }
  return bdpcmAllowed;
}

bool CU::isMTSAllowed(const CodingUnit &cu, const ComponentID compID)
{
  SizeType tsMaxSize = 1 << cu.cs->sps->getLog2MaxTransformSkipBlockSize();
  const int maxSize  = CU::isIntra( cu ) ? MTS_INTRA_MAX_CU_SIZE : MTS_INTER_MAX_CU_SIZE;
  const int cuWidth  = cu.blocks[0].lumaSize().width;
  const int cuHeight = cu.blocks[0].lumaSize().height;
  bool mtsAllowed    = cu.chType == CHANNEL_TYPE_LUMA && compID == COMPONENT_Y;

  mtsAllowed &= CU::isIntra(cu) ? cu.cs->sps->getExplicitMtsIntraEnabled()
                                : cu.cs->sps->getExplicitMtsInterEnabled() && CU::isInter(cu);
  mtsAllowed &= cuWidth <= maxSize && cuHeight <= maxSize;
  mtsAllowed &= !cu.ispMode;
  mtsAllowed &= !cu.sbtInfo;
  mtsAllowed &= !(cu.bdpcmMode != BdpcmMode::NONE && cuWidth <= tsMaxSize && cuHeight <= tsMaxSize);
  return mtsAllowed;
}

// TU tools

bool TU::isNonTransformedResidualRotated(const TransformUnit &tu, const ComponentID &compID)
{
  return tu.cs->sps->getSpsRangeExtension().getTransformSkipRotationEnabledFlag() && tu.blocks[compID].width == 4
         && CU::isIntra(*tu.cu);
}

bool TU::getCbf( const TransformUnit &tu, const ComponentID &compID )
{
  return getCbfAtDepth( tu, compID, tu.depth );
}

bool TU::getCbfAtDepth(const TransformUnit &tu, const ComponentID &compID, const unsigned &depth)
{
  if( !tu.blocks[compID].valid() )
  {
    CHECK(tu.cbf[compID] != 0, "cbf must be 0 if the component is not available");
  }
  return ((tu.cbf[compID] >> depth) & 1) == 1;
}

void TU::setCbfAtDepth(TransformUnit &tu, const ComponentID &compID, const unsigned &depth, const bool &cbf)
{
  // first clear the CBF at the depth
  tu.cbf[compID] &= ~(1  << depth);
  // then set the CBF
  tu.cbf[compID] |= ((cbf ? 1 : 0) << depth);
}

bool TU::isTSAllowed(const TransformUnit &tu, const ComponentID compID)
{
  const int maxSize = tu.cs->sps->getLog2MaxTransformSkipBlockSize();

  bool tsAllowed = tu.cs->sps->getTransformSkipEnabledFlag();
  tsAllowed &= ( !tu.cu->ispMode || !isLuma(compID) );
  SizeType transformSkipMaxSize = 1 << maxSize;
  tsAllowed &= tu.cu->getBdpcmMode(compID) == BdpcmMode::NONE;
  tsAllowed &= tu.blocks[compID].width <= transformSkipMaxSize && tu.blocks[compID].height <= transformSkipMaxSize;
  tsAllowed &= !tu.cu->sbtInfo;

  return tsAllowed;
}


int TU::getICTMode( const TransformUnit& tu, int jointCbCr )
{
  if( jointCbCr < 0 )
  {
    jointCbCr = tu.jointCbCr;
  }
  return g_ictModes[ tu.cs->picHeader->getJointCbCrSignFlag() ][ jointCbCr ];
}


bool TU::needsSqrt2Scale( const TransformUnit &tu, const ComponentID &compID )
{
  const Size &size=tu.blocks[compID];
  const bool isTransformSkip = (tu.mtsIdx[compID] == MTS_SKIP);
  return (!isTransformSkip) && (((floorLog2(size.width) + floorLog2(size.height)) & 1) == 1);
}

bool TU::needsBlockSizeTrafoScale( const TransformUnit &tu, const ComponentID &compID )
{
  return needsSqrt2Scale( tu, compID ) || isNonLog2BlockSize( tu.blocks[compID] );
}

TransformUnit* TU::getPrevTU( const TransformUnit &tu, const ComponentID compID )
{
  TransformUnit* prevTU = tu.prev;

  if( prevTU != nullptr && ( prevTU->cu != tu.cu || !prevTU->blocks[compID].valid() ) )
  {
    prevTU = nullptr;
  }

  return prevTU;
}

bool TU::getPrevTuCbfAtDepth( const TransformUnit &currentTu, const ComponentID compID, const int trDepth )
{
  const TransformUnit* prevTU = getPrevTU( currentTu, compID );
  return ( prevTU != nullptr ) ? TU::getCbfAtDepth( *prevTU, compID, trDepth ) : false;
}

// other tools

uint32_t getCtuAddr( const Position& pos, const PreCalcValues& pcv )
{
  return ( pos.x >> pcv.maxCUWidthLog2 ) + ( pos.y >> pcv.maxCUHeightLog2 ) * pcv.widthInCtus;
}

int getNumModesMip(const Size& block)
{
  switch( getMipSizeId(block) )
  {
  case 0: return 16;
  case 1: return  8;
  case 2: return  6;
  default: THROW( "Invalid mipSizeId" );
  }
}

int getMipSizeId(const Size& block)
{
  if( block.width == 4 && block.height == 4 )
  {
    return 0;
  }
  else if( block.width == 4 || block.height == 4 || (block.width == 8 && block.height == 8) )
  {
    return 1;
  }
  else
  {
    return 2;
  }
}

bool allowLfnstWithMip(const Size& block)
{
  if (block.width >= 16 && block.height >= 16)
  {
    return true;
  }
  return false;
}


#if GREEN_METADATA_SEI_ENABLED
void writeGMFAOutput(FeatureCounterStruct& featureCounterUpdated, FeatureCounterStruct& featureCounterOld, std::string GMFAFile,bool lastFrame )
{
  std::string fileName = std::string("");
  std::string matlabFile = std::string("");
  if (GMFAFile.length() == 0)
  {
    return;
  }
  fileName = GMFAFile;
  size_t strStart = 0;
  size_t strEnd = 0;
  
  int64_t codedFrames = featureCounterUpdated.iSlices + featureCounterUpdated.bSlices + featureCounterUpdated.pSlices;
  strEnd = fileName.rfind('.');
  strEnd = strEnd == fileName.length() ? 0 : strEnd;

  if (!lastFrame)
  {
    fileName = fileName.substr(0, strEnd) + "_" + std::to_string(codedFrames) + fileName.substr(strEnd, fileName.length());
  }
  
  strStart = fileName.find_last_of("/\\");
  strStart = strStart == fileName.length() ? 0 : strStart+1;

  matlabFile = fileName.substr(strStart, strEnd-strStart);
  std::ofstream featureFile(fileName);

  featureFile << "function [n]=";
  featureFile << matlabFile.c_str();
  featureFile << "() \n";
  //General features
  featureFile << "\tn.EO = 1;\n";
  featureFile << "\tn.ISlice = " << featureCounterUpdated.iSlices-featureCounterOld.iSlices << ";\n";
  featureFile << "\tn.PSlice = " << featureCounterUpdated.pSlices-featureCounterOld.pSlices << ";\n";
  featureFile << "\tn.BSlice = " << featureCounterUpdated.bSlices-featureCounterOld.bSlices << ";\n";
  
  featureFile << "\tn.width  = " << featureCounterUpdated.width << " ; \n";
  featureFile << "\tn.height = " << featureCounterUpdated.height << " ; \n";
  featureFile << "\tn.baseQP = [...\n\t";
  for (int iter = 0; iter < 64; iter++)
  {
    featureFile << " " << featureCounterUpdated.baseQP[iter]-featureCounterOld.baseQP[iter] << " ";
    if (iter % 8 == 7 && iter != 63)
    {
      featureFile << " ...\n\t";
    }
  }
  featureFile << "]; \n ";
  
  featureFile << "\tn.bytes  = " << featureCounterUpdated.bytes << " ; \n";
  featureFile << "\tn.is8bit = " << featureCounterUpdated.is8bit << " ; \n";
  featureFile << "\tn.is10bit = " << featureCounterUpdated.is10bit << " ; \n";
  featureFile << "\tn.is12bit = " << featureCounterUpdated.is12bit << " ; \n";
  featureFile << "\tn.isYUV400 = " << featureCounterUpdated.isYUV400 << " ; \n";
  featureFile << "\tn.isYUV420 = " << featureCounterUpdated.isYUV420 << " ; \n";
  featureFile << "\tn.isYUV422 = " << featureCounterUpdated.isYUV422 << " ; \n";
  featureFile << "\tn.isYUV444 = " << featureCounterUpdated.isYUV444 << " ; \n";
  //Intra Feature
  featureToFile(featureFile, featureCounterUpdated.intraBlockSizes, "intraBlocks",true,featureCounterOld.intraBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaPlaBlockSizes, "intraPlaLuma",true,featureCounterOld.intraLumaPlaBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaDcBlockSizes, "intraDCLuma",true,featureCounterOld.intraLumaDcBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaHvdBlockSizes, "intraHVDLuma",true,featureCounterOld.intraLumaHvdBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaHvBlockSizes, "intraHVLuma",true,featureCounterOld.intraLumaHvBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaAngBlockSizes, "intraAngLuma",true,featureCounterOld.intraLumaAngBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaPlaBlockSizes, "intraPlaChroma",true,featureCounterOld.intraChromaPlaBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaDcBlockSizes, "intraDCChroma",true,featureCounterOld.intraChromaDcBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaHvdBlockSizes, "intraHVDChroma",true,featureCounterOld.intraChromaHvdBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaHvBlockSizes, "intraHVChroma",true,featureCounterOld.intraChromaHvBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaAngBlockSizes, "intraAngChroma",true,featureCounterOld.intraChromaAngBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaCrossCompBlockSizes, "intraCrossComp",true,featureCounterOld.intraChromaCrossCompBlockSizes);
  
  featureToFile(featureFile, featureCounterUpdated.intraPDPCBlockSizes, "intraPDPC",true,featureCounterOld.intraPDPCBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaPDPCBlockSizes, "intraLumaPDPC",true,featureCounterOld.intraLumaPDPCBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaPDPCBlockSizes, "intraChromaPDPC",true,featureCounterOld.intraChromaPDPCBlockSizes);
  
  featureToFile(featureFile, featureCounterUpdated.intraMIPBlockSizes, "intraMIP",true,featureCounterOld.intraMIPBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraLumaMIPBlockSizes, "intraLumaMIP",true,featureCounterOld.intraLumaMIPBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.intraChromaMIPBlockSizes, "intraChromaMIP",true,featureCounterOld.intraChromaMIPBlockSizes);
  
  featureToFile(featureFile, featureCounterUpdated.IBCBlockSizes, "intraIBC",true,featureCounterOld.IBCBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.IBCLumaBlockSizes, "intraLumaIBC",true,featureCounterOld.IBCLumaBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.IBCChromaBlockSizes, "intraChromaIBC",true,featureCounterOld.IBCChromaBlockSizes);
  
  featureToFile(featureFile, featureCounterUpdated.intraSubPartitionsHorizontal, "intraSubPartitionsHorizontal",true,featureCounterOld.intraSubPartitionsHorizontal);
  featureToFile(featureFile, featureCounterUpdated.intraLumaSubPartitionsHorizontal, "intraLumaSubPartitionsHorizontal",true,featureCounterOld.intraLumaSubPartitionsHorizontal);
  featureToFile(featureFile, featureCounterUpdated.intraChromaSubPartitionsHorizontal,"intraChromaSubPartitionsHorizontal",true,featureCounterOld.intraChromaSubPartitionsHorizontal);
  
  featureToFile(featureFile, featureCounterUpdated.intraSubPartitionsVertical, "intraSubPartitionsVertical",true,featureCounterOld.intraSubPartitionsVertical);
  featureToFile(featureFile, featureCounterUpdated.intraLumaSubPartitionsVertical, "intraLumaSubPartitionsVertical",true,featureCounterOld.intraLumaSubPartitionsVertical);
  featureToFile(featureFile, featureCounterUpdated.intraChromaSubPartitionsVertical, "intraChromaSubPartitionsVertical",true,featureCounterOld.intraChromaSubPartitionsVertical);
  
  //Inter
  featureToFile(featureFile, featureCounterUpdated.interBlockSizes, "interBlocks",true,featureCounterOld.interBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.interLumaBlockSizes, "interLumaBlocks",true,featureCounterOld.interLumaBlockSizes);
  featureToFile(featureFile, featureCounterUpdated.interChromaBlockSizes, "interChromaBlocks",true,featureCounterOld.interChromaBlockSizes);
  
  featureToFile(featureFile, featureCounterUpdated.interInterBlocks, "interInter",true,featureCounterOld.interInterBlocks);
  featureToFile(featureFile, featureCounterUpdated.interLumaInterBlocks, "interLumaInter",true,featureCounterOld.interLumaInterBlocks);
  featureToFile(featureFile, featureCounterUpdated.interChromaInterBlocks, "interChromaInter",true,featureCounterOld.interChromaInterBlocks);
  
  featureToFile(featureFile, featureCounterUpdated.interMergeBlocks, "interMerge",true,featureCounterOld.interMergeBlocks);
  featureToFile(featureFile, featureCounterUpdated.interLumaMergeBlocks, "interLumaMerge",true,featureCounterOld.interLumaMergeBlocks);
  featureToFile(featureFile, featureCounterUpdated.interChromaMergeBlocks, "interChromaMerge",true,featureCounterOld.interChromaMergeBlocks);
  
  featureToFile(featureFile, featureCounterUpdated.interSkipBlocks, "interSkip",true,featureCounterOld.interSkipBlocks);
  featureToFile(featureFile, featureCounterUpdated.interLumaSkipBlocks, "interLumaSkip",true,featureCounterOld.interLumaSkipBlocks);
  featureToFile(featureFile, featureCounterUpdated.interChromaSkipBlocks, "interChromaSkip",true,featureCounterOld.interChromaSkipBlocks);
  
  featureToFile(featureFile, featureCounterUpdated.affine, "interAffine",true,featureCounterOld.affine);
  featureToFile(featureFile, featureCounterUpdated.affineLuma, "interLumaAffine",true,featureCounterOld.affineLuma);
  featureToFile(featureFile, featureCounterUpdated.affineChroma, "interChromaAffine",true,featureCounterOld.affineChroma);
  
  featureToFile(featureFile, featureCounterUpdated.affineInter, "interAffineInter",true,featureCounterOld.affineInter);
  featureToFile(featureFile, featureCounterUpdated.affineLumaInter, "interLumaAffineInter",true,featureCounterOld.affineLumaInter);
  featureToFile(featureFile, featureCounterUpdated.affineChromaInter, "interChromaAffineInter",true,featureCounterOld.affineChromaInter);
  
  featureToFile(featureFile, featureCounterUpdated.affineMerge, "interAffineMerge",true,featureCounterOld.affineMerge);
  featureToFile(featureFile, featureCounterUpdated.affineLumaMerge, "interLumaAffineMerge",true,featureCounterOld.affineLumaMerge);
  featureToFile(featureFile, featureCounterUpdated.affineChromaMerge, "interChromaAffineMerge",true,featureCounterOld.affineChromaMerge);
  
  featureToFile(featureFile, featureCounterUpdated.affineSkip, "interAffineSkip",true,featureCounterOld.affineSkip);
  featureToFile(featureFile, featureCounterUpdated.affineLumaSkip, "interLumaAffineSkip",true,featureCounterOld.affineLumaSkip);
  featureToFile(featureFile, featureCounterUpdated.affineChromaSkip, "interChromaAffineSkip",true,featureCounterOld.affineChromaSkip);
  
  featureToFile(featureFile, featureCounterUpdated.geo, "geo",true,featureCounterOld.geo);
  featureToFile(featureFile, featureCounterUpdated.geoLuma, "geoLuma",true,featureCounterOld.geoLuma);
  featureToFile(featureFile, featureCounterUpdated.geoChroma, "geoChroma",true,featureCounterOld.geoChroma);
  
  featureToFile(featureFile, featureCounterUpdated.dmvrBlocks, "dmvrBlocks",true,featureCounterOld.dmvrBlocks);
  featureToFile(featureFile, featureCounterUpdated.bdofBlocks, "bdofBlocks",true,featureCounterOld.bdofBlocks);

  featureFile << "\tn.uniPredPel  = " << featureCounterUpdated.uniPredPel-featureCounterOld.uniPredPel << " ; \n";
  featureFile << "\tn.biPredPel   = " << featureCounterUpdated.biPredPel-featureCounterOld.biPredPel << " ; \n";
  featureFile << "\tn.fracPelHor  = " << featureCounterUpdated.fracPelHor-featureCounterOld.fracPelHor << " ; \n";
  featureFile << "\tn.fracPelVer  = " << featureCounterUpdated.fracPelVer-featureCounterOld.fracPelVer << " ; \n";
  featureFile << "\tn.fracPelBoth = " << featureCounterUpdated.fracPelBoth-featureCounterOld.fracPelBoth << " ; \n";
  featureFile << "\tn.copyCUPel   = " << featureCounterUpdated.copyCUPel-featureCounterOld.copyCUPel << " ; \n";
  
  featureFile << "\tn.affineFracPelHor  = " << featureCounterUpdated.affineFracPelHor-featureCounterOld.affineFracPelHor << " ; \n";
  featureFile << "\tn.affineFracPelVer  = " << featureCounterUpdated.affineFracPelVer-featureCounterOld.affineFracPelVer << " ; \n";
  featureFile << "\tn.affineFracPelBoth = " << featureCounterUpdated.affineFracPelBoth-featureCounterOld.affineFracPelBoth << " ; \n";
  featureFile << "\tn.affineCopyCUPel   = " << featureCounterUpdated.affineCopyCUPel-featureCounterOld.affineCopyCUPel << " ; \n";
  
  //Transform
  featureToFile(featureFile, featureCounterUpdated.transformBlocks, "transform",true,featureCounterOld.transformBlocks);
  featureToFile(featureFile, featureCounterUpdated.transformLumaBlocks, "transformLuma",true,featureCounterOld.transformLumaBlocks);
  featureToFile(featureFile, featureCounterUpdated.transformChromaBlocks, "transformChroma",true,featureCounterOld.transformChromaBlocks);
  
  featureToFile(featureFile, featureCounterUpdated.transformSkipBlocks, "transformSkip",true,featureCounterOld.transformSkipBlocks);
  featureToFile(featureFile, featureCounterUpdated.transformLumaSkipBlocks, "transformLumaSkip",true,featureCounterOld.transformLumaSkipBlocks);
  featureToFile(featureFile, featureCounterUpdated.transformChromaSkipBlocks, "transformChromaSkip",true,featureCounterOld.transformChromaSkipBlocks);
  
  featureFile << "\tn.transformLFNST4  = " << featureCounterUpdated.transformLFNST4-featureCounterOld.transformLFNST4 << " ; \n";
  featureFile << "\tn.transformLFNST8  = " << featureCounterUpdated.transformLFNST8-featureCounterOld.transformLFNST8 << " ; \n";
  
  featureFile << "\tn.nrOfCoeff  = " << featureCounterUpdated.nrOfCoeff-featureCounterOld.nrOfCoeff << " ; \n";
  featureFile << "\tn.coeffG1    = " << featureCounterUpdated.coeffG1-featureCounterOld.coeffG1 << " ; \n";
  featureFile << "\tn.valueOfCoeff  = " << featureCounterUpdated.valueOfCoeff-featureCounterOld.valueOfCoeff << " ; \n";
  //In-Loop
  featureFile << "\tn.BS0  = " << featureCounterUpdated.boundaryStrength[0]-featureCounterOld.boundaryStrength[0] << " ; \n";
  featureFile << "\tn.BS1  = " << featureCounterUpdated.boundaryStrength[1]-featureCounterOld.boundaryStrength[1] << " ; \n";
  featureFile << "\tn.BS2  = " << featureCounterUpdated.boundaryStrength[2]-featureCounterOld.boundaryStrength[2] << " ; \n";
  featureFile << "\tn.BSPel0  = " << featureCounterUpdated.boundaryStrengthPel[0]-featureCounterOld.boundaryStrengthPel[0] << " ; \n";
  featureFile << "\tn.BSPel1  = " << featureCounterUpdated.boundaryStrengthPel[1]-featureCounterOld.boundaryStrengthPel[1] << " ; \n";
  featureFile << "\tn.BSPel2  = " << featureCounterUpdated.boundaryStrengthPel[2]-featureCounterOld.boundaryStrengthPel[2] << " ; \n";
  featureFile << "\tn.saoLumaBO  = " << featureCounterUpdated.saoLumaBO-featureCounterOld.saoLumaBO << " ; \n";
  featureFile << "\tn.saoLumaEO  = " << featureCounterUpdated.saoLumaEO-featureCounterOld.saoLumaEO << " ; \n";
  featureFile << "\tn.saoChromaBO  = " << featureCounterUpdated.saoChromaBO-featureCounterOld.saoChromaBO << " ; \n";
  featureFile << "\tn.saoChromaEO  = " << featureCounterUpdated.saoChromaEO-featureCounterOld.saoChromaEO << " ; \n";
  featureFile <<   "\tn.saoLumaPels  = " << featureCounterUpdated.saoLumaPels << " ; \n";
  featureFile <<   "\tn.saoChromaPels  = " << featureCounterUpdated.saoChromaPels << " ; \n";
  featureFile << "\tn.alfLumaType7  = " << featureCounterUpdated.alfLumaType7-featureCounterOld.alfLumaType7 << " ; \n";
  featureFile << "\tn.alfChromaType5 = " << featureCounterUpdated.alfChromaType5-featureCounterOld.alfChromaType5 << " ; \n";
  featureFile <<   "\tn.alfLumaPels  = " << featureCounterUpdated.alfLumaPels << " ; \n";
  featureFile <<   "\tn.alfChromaPels  = " << featureCounterUpdated.alfChromaPels << " ; \n";
  featureFile << "\tn.ccalf = " << featureCounterUpdated.ccalf-featureCounterOld.ccalf  << " ; \n";
  featureFile << "end\n";
  featureFile.close();
}

void featureToFile(std::ofstream& featureFile,int featureCounterReference[MAX_CU_DEPTH+1][MAX_CU_DEPTH+1], std::string featureName,bool calcDifference,int featureCounter[MAX_CU_DEPTH+1][MAX_CU_DEPTH+1])
{
  featureFile <<   "\tn." << featureName << " = [...\n\t";
  for (size_t i = 0; i < MAX_CU_DEPTH+1; i++)
  {
    for (size_t j = 0; j < MAX_CU_DEPTH+1; j++)
    {
      if (calcDifference)
      {
        featureFile << " " << featureCounterReference[j][i]  - featureCounter[j][i] << " ";
      }
      else
      {
        featureFile << " " << featureCounterReference[j][i]  << " ";
      }
    }
    if (i != MAX_CU_DEPTH)
    {
      featureFile << ";... \n\t";
    }
  }
  featureFile << "]; \n ";
}

void countFeatures(FeatureCounterStruct& featureCounter, CodingStructure& cs, const UnitArea& ctuArea)
{
  SizeType             cuWidthIdx     = MAX_UINT;
  SizeType             cuHeightIdx    = MAX_UINT;
  
  for (auto &currCU: cs.traverseCUs(ctuArea, CHANNEL_TYPE_LUMA))
  {
    cuWidthIdx  = floorLog2(currCU.lwidth());
    cuHeightIdx = floorLog2(currCU.lheight());
    if ((cuWidthIdx <= MAX_CU_DEPTH+1) && (cuHeightIdx <= MAX_CU_DEPTH+1))
    {
      if (currCU.predMode == MODE_INTRA)   // Intra-Mode
      {
        for (auto &currPU: CU::traversePUs(currCU))
        {
          SizeType puWidthIdx  = floorLog2(currPU.Y().width);
          SizeType puHeightIdx = floorLog2(currPU.Y().height);
          if ((puWidthIdx <= MAX_CU_DEPTH+1) && (puHeightIdx <= MAX_CU_DEPTH+1))
          {
            featureCounter.intraBlockSizes[puWidthIdx][puHeightIdx]++;
            SizeType lumaPredDir = currPU.intraDir[CHANNEL_TYPE_LUMA];
            if (currCU.mipFlag)
            {
              featureCounter.intraMIPBlockSizes[puWidthIdx][puHeightIdx]++;
              featureCounter.intraLumaMIPBlockSizes[puWidthIdx][puHeightIdx]++;
            }
            else
            {
              if (lumaPredDir == PLANAR_IDX)
              {
                featureCounter.intraLumaPlaBlockSizes[puWidthIdx][puHeightIdx]++;
              }
              else if (lumaPredDir == DC_IDX)
              {
                featureCounter.intraLumaDcBlockSizes[puWidthIdx][puHeightIdx]++;
              }
              else if (lumaPredDir == HOR_IDX || lumaPredDir == VER_IDX || lumaPredDir == DIA_IDX)
              {
                featureCounter.intraLumaHvdBlockSizes[puWidthIdx][puHeightIdx]++;
              }
              else
              {
                featureCounter.intraLumaAngBlockSizes[puWidthIdx][puHeightIdx]++;
              }
            }
            bool tempPDPC =
              (currPU.Y().width >= MIN_TB_SIZEY && currPU.Y().height >= MIN_TB_SIZEY) && isLuma(COMPONENT_Y)
                ? currPU.multiRefIdx
                : false;
            if (tempPDPC)
            {
              featureCounter.intraPDPCBlockSizes[puWidthIdx][puHeightIdx]++;
              featureCounter.intraLumaPDPCBlockSizes[puWidthIdx][puHeightIdx]++;
            }
          }
        }

        if (currCU.ispMode > NOT_INTRA_SUBPARTITIONS)
        {
          if (currCU.ispMode == VER_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsVertical[cuWidthIdx][cuHeightIdx]++;
            featureCounter.intraLumaSubPartitionsVertical[cuWidthIdx][cuHeightIdx]++;
          }
          if (currCU.ispMode == HOR_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsHorizontal[cuWidthIdx][cuHeightIdx]++;
            featureCounter.intraLumaSubPartitionsHorizontal[cuWidthIdx][cuHeightIdx]++;
          }
        }
      }
      else if (currCU.predMode == MODE_IBC)   // IBC-Mode
      {
        featureCounter.IBCBlockSizes[cuWidthIdx][cuHeightIdx]++;
        featureCounter.IBCLumaBlockSizes[cuWidthIdx][cuHeightIdx]++;
      }
      else if (currCU.predMode == MODE_INTER)   // Inter-Mode
      {
        featureCounter.interBlockSizes[cuWidthIdx][cuHeightIdx]++;
        featureCounter.interLumaBlockSizes[cuWidthIdx][cuHeightIdx]++;
        featureCounter.interBlockSizes[cuWidthIdx-1][cuHeightIdx-1] += 2;
        featureCounter.interChromaBlockSizes[cuWidthIdx-1][cuHeightIdx-1] += 2;
        if (currCU.skip)
        {
          featureCounter.interSkipBlocks[cuWidthIdx][cuHeightIdx]++;
          featureCounter.interLumaSkipBlocks[cuWidthIdx][cuHeightIdx]++;
          featureCounter.interSkipBlocks[cuWidthIdx-1][cuHeightIdx-1] += 2;
          featureCounter.interChromaSkipBlocks[cuWidthIdx-1][cuHeightIdx-1] += 2;
          if (currCU.affine)
          {
            featureCounter.affine[cuWidthIdx][cuHeightIdx]++;
            featureCounter.affineLuma[cuWidthIdx][cuHeightIdx]++;
            featureCounter.affineSkip[cuWidthIdx][cuHeightIdx]++;
            featureCounter.affineLumaSkip[cuWidthIdx][cuHeightIdx]++;
            featureCounter.affine[cuWidthIdx-1][cuHeightIdx-1] += 2;
            featureCounter.affineChroma[cuWidthIdx-1][cuHeightIdx-1] += 2;
            featureCounter.affineSkip[cuWidthIdx-1][cuHeightIdx-1] += 2;
            featureCounter.affineChromaSkip[cuWidthIdx-1][cuHeightIdx-1] += 2;
          }
        }
        else
        {
          for (auto &currPU: CU::traversePUs(currCU))
          {
            SizeType puWidthIdx  = floorLog2(currPU.lwidth());
            SizeType puHeightIdx = floorLog2(currPU.lheight());
            if ((puWidthIdx <= MAX_CU_DEPTH+1) && (puHeightIdx <= MAX_CU_DEPTH+1))
            {
              if (currPU.mergeFlag)
              {
                featureCounter.interMergeBlocks[puWidthIdx][puHeightIdx]++;
                featureCounter.interLumaMergeBlocks[puWidthIdx][puHeightIdx]++;
                featureCounter.interMergeBlocks[puWidthIdx-1][puHeightIdx-1] += 2;
                featureCounter.interChromaMergeBlocks[puWidthIdx-1][puHeightIdx-1] += 2;
                if (currCU.affine)
                {
                  featureCounter.affine[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineLuma[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineMerge[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineLumaMerge[puWidthIdx][puHeightIdx]++;
                  featureCounter.affine[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineChroma[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineMerge[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineChromaMerge[puWidthIdx-1][puHeightIdx-1] += 2;
                }
              }
              else   // InterInter
              {
                featureCounter.interInterBlocks[puWidthIdx][puHeightIdx]++;
                featureCounter.interLumaInterBlocks[puWidthIdx][puHeightIdx]++;
                featureCounter.interInterBlocks[puWidthIdx-1][puHeightIdx-1] += 2;
                featureCounter.interChromaInterBlocks[puWidthIdx-1][puHeightIdx-1] += 2;
                if (currCU.affine)
                {
                  featureCounter.affine[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineLuma[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineInter[puWidthIdx][puHeightIdx]++;
                  featureCounter.affineLumaInter[puWidthIdx][puHeightIdx]++;
                  featureCounter.affine[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineChroma[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineInter[puWidthIdx-1][puHeightIdx-1] += 2;
                  featureCounter.affineChromaInter[puWidthIdx-1][puHeightIdx-1] += 2;
                }
              }
              if (currPU.cu->geoFlag)
              {
                featureCounter.geo[puWidthIdx][puHeightIdx]++;
                featureCounter.geoLuma[puWidthIdx][puHeightIdx]++;
                featureCounter.geo[puWidthIdx-1][puHeightIdx-1] += 2;
                featureCounter.geoChroma[puWidthIdx-1][puHeightIdx-1] += 2;
              }
            }
          }
        }



        for (auto &currPU: CU::traversePUs(currCU))   // Check whether BDOF or DMVR are used in the current PU
        {
          bool m_DMVRapplied = false;
          bool m_BDOFapplied = false;

          PredictionUnit &pu          = currPU;
          SizeType        puWidthIdx  = floorLog2(currPU.lwidth());
          SizeType        puHeightIdx = floorLog2(currPU.lheight());
          if ((puWidthIdx <= MAX_CU_DEPTH + 1) && (puHeightIdx <= MAX_CU_DEPTH + 1))
          {
            const PPS   &pps        = *pu.cs->pps;
            const Slice &localSlice = *pu.cs->slice;
            CHECK(!pu.cu->affine && pu.refIdx[0] >= 0 && pu.refIdx[1] >= 0 && (pu.lwidth() + pu.lheight() == 12),
                  "invalid 4x8/8x4 bi-predicted blocks");

            int refIdx0 = pu.refIdx[REF_PIC_LIST_0];
            int refIdx1 = pu.refIdx[REF_PIC_LIST_1];

            const WPScalingParam *wp0 = pu.cs->slice->getWpScaling(REF_PIC_LIST_0, refIdx0);
            const WPScalingParam *wp1 = pu.cs->slice->getWpScaling(REF_PIC_LIST_1, refIdx1);

            bool bioApplied = false;
            if (pu.cs->sps->getBDOFEnabledFlag() && (!pu.cs->picHeader->getBdofDisabledFlag()))
            {
              if (pu.cu->affine)
              {
                bioApplied = false;
              }
              else
              {
                const bool biocheck0 = !((WPScalingParam::isWeighted(wp0) || WPScalingParam::isWeighted(wp1))
                                         && localSlice.getSliceType() == B_SLICE);
                const bool biocheck1 = !(pps.getUseWP() && localSlice.getSliceType() == P_SLICE);
                if (biocheck0 && biocheck1 && PU::isSimpleSymmetricBiPred(pu) && (pu.Y().height >= 8)
                    && (pu.Y().width >= 8) && ((pu.Y().height * pu.Y().width) >= 128))
                {
                  bioApplied = true;
                }
              }

              if (bioApplied && pu.ciipFlag)
              {
                bioApplied = false;
              }

              if (bioApplied && pu.cu->smvdMode)
              {
                bioApplied = false;
              }

              if (pu.cu->cs->sps->getUseBcw() && bioApplied && pu.cu->bcwIdx != BCW_DEFAULT)
              {
                bioApplied = false;
              }
            }
            if (pu.mmvdEncOptMode == 2 && pu.mmvdMergeFlag)
            {
              bioApplied = false;
            }
            bool dmvrApplied = false;
            dmvrApplied      = PU::checkDMVRCondition(pu);

            bool refIsScaled =
              (refIdx0 < 0 ? false : pu.cu->slice->getRefPic(REF_PIC_LIST_0, refIdx0)->isRefScaled(pu.cs->pps))
              || (refIdx1 < 0 ? false : pu.cu->slice->getRefPic(REF_PIC_LIST_1, refIdx1)->isRefScaled(pu.cs->pps));
            dmvrApplied = dmvrApplied && !refIsScaled;
            bioApplied  = bioApplied && !refIsScaled;

            for (uint32_t refList = 0; refList < NUM_REF_PIC_LIST_01; refList++)
            {
              if (pu.refIdx[refList] < 0)
              {
                continue;
              }

              RefPicList eRefPicList = (refList ? REF_PIC_LIST_1 : REF_PIC_LIST_0);

              CHECK(CU::isIBC(*pu.cu) && eRefPicList != REF_PIC_LIST_0, "Invalid interdir for ibc mode");
              CHECK(CU::isIBC(*pu.cu) && pu.refIdx[refList] != MAX_NUM_REF, "Invalid reference index for ibc mode");
              CHECK((CU::isInter(*pu.cu) && pu.refIdx[refList] >= localSlice.getNumRefIdx(eRefPicList)),
                    "Invalid reference index");
            }

            m_BDOFapplied = bioApplied;

            if (!pu.cu->geoFlag && (!dmvrApplied) && (!bioApplied) && pps.getWPBiPred()
                && localSlice.getSliceType() == B_SLICE && pu.cu->bcwIdx == BCW_DEFAULT)
            {
              m_DMVRapplied = false;
            }
            else if (!pu.cu->geoFlag && pps.getUseWP() && localSlice.getSliceType() == P_SLICE)
            {
              m_DMVRapplied = false;
            }
            else
            {
              m_DMVRapplied = dmvrApplied;
            }

            if (m_DMVRapplied)
            {
              featureCounter.dmvrBlocks[puWidthIdx][puHeightIdx]++;
            }

            if (m_BDOFapplied)
            {
              featureCounter.bdofBlocks[puWidthIdx][puHeightIdx]++;
            }
          }
        }
      }

      if (currCU.predMode == MODE_INTER)
      {
        const int nShift  = MV_FRACTIONAL_BITS_DIFF;
        const int nOffset = 1 << (nShift - 1);

        for (auto &currPU: CU::traversePUs(currCU))
        {
          int numberOfPels = currPU.lwidth() * currPU.lheight();
          if (!currPU.cu->affine && !currPU.cu->geoFlag)
          {
            if (currPU.interDir != MODE_TYPE_INTRA /* PRED_L1 */)
            {
              Mv mv   = currPU.mv[REF_PIC_LIST_0];
              Mv mvd  = currPU.mvd[REF_PIC_LIST_0];
              mv.hor  = mv.hor >= 0 ? (mv.hor + nOffset) >> nShift : -((-mv.hor + nOffset) >> nShift);
              mv.ver  = mv.ver >= 0 ? (mv.ver + nOffset) >> nShift : -((-mv.ver + nOffset) >> nShift);
              mvd.hor = mvd.hor >= 0 ? (mvd.hor + nOffset) >> nShift : -((-mvd.hor + nOffset) >> nShift);
              mvd.ver = mvd.ver >= 0 ? (mvd.ver + nOffset) >> nShift : -((-mvd.ver + nOffset) >> nShift);

              if (currPU.interDir == 3)
              {
                featureCounter.biPredPel += numberOfPels;
              }
              else
              {
                featureCounter.uniPredPel += numberOfPels;
              }

              if (mv.hor == 0 && mv.ver == 0)
              {
                featureCounter.copyCUPel += numberOfPels;
              }
              if (mv.hor != 0 && mv.ver == 0)
              {
                featureCounter.fracPelHor += numberOfPels;
              }
              if (mv.hor == 0 && mv.ver != 0)
              {
                featureCounter.fracPelVer += numberOfPels;
              }
              if (mv.hor != 0 && mv.ver != 0)
              {
                featureCounter.fracPelBoth += numberOfPels;
              }
            }
            if (currPU.interDir != 1 /* PRED_L1 */)
            {
              Mv mv   = currPU.mv[REF_PIC_LIST_1];
              Mv mvd  = currPU.mvd[REF_PIC_LIST_1];
              mv.hor  = mv.hor >= 0 ? (mv.hor + nOffset) >> nShift : -((-mv.hor + nOffset) >> nShift);
              mv.ver  = mv.ver >= 0 ? (mv.ver + nOffset) >> nShift : -((-mv.ver + nOffset) >> nShift);
              mvd.hor = mvd.hor >= 0 ? (mvd.hor + nOffset) >> nShift : -((-mvd.hor + nOffset) >> nShift);
              mvd.ver = mvd.ver >= 0 ? (mvd.ver + nOffset) >> nShift : -((-mvd.ver + nOffset) >> nShift);

              if (currPU.interDir != 3)
              {
                featureCounter.uniPredPel += numberOfPels;
              }

              if (mv.hor == 0 && mv.ver == 0)
              {
                featureCounter.copyCUPel += numberOfPels;
              }
              if (mv.hor != 0 && mv.ver == 0)
              {
                featureCounter.fracPelHor += numberOfPels;
              }
              if (mv.hor == 0 && mv.ver != 0)
              {
                featureCounter.fracPelVer += numberOfPels;
              }
              if (mv.hor != 0 && mv.ver != 0)
              {
                featureCounter.fracPelBoth += numberOfPels;
              }
            }
          }
          else if (currPU.cu->affine)
          {
            if (currPU.interDir != 2 /* PRED_L1 */)
            {
              Mv                mv[3];
              const CMotionBuf &mb = currPU.getMotionBuf();
              mv[0]                = mb.at(0, 0).mv[REF_PIC_LIST_0];
              mv[1]                = mb.at(mb.width - 1, 0).mv[REF_PIC_LIST_0];
              mv[2]                = mb.at(0, mb.height - 1).mv[REF_PIC_LIST_0];
              mv[0].hor = mv[0].hor >= 0 ? (mv[0].hor + nOffset) >> nShift : -((-mv[0].hor + nOffset) >> nShift);
              mv[0].ver = mv[0].ver >= 0 ? (mv[0].ver + nOffset) >> nShift : -((-mv[0].ver + nOffset) >> nShift);
              mv[1].hor = mv[1].hor >= 0 ? (mv[1].hor + nOffset) >> nShift : -((-mv[1].hor + nOffset) >> nShift);
              mv[1].ver = mv[1].ver >= 0 ? (mv[1].ver + nOffset) >> nShift : -((-mv[1].ver + nOffset) >> nShift);
              mv[2].hor = mv[2].hor >= 0 ? (mv[2].hor + nOffset) >> nShift : -((-mv[2].hor + nOffset) >> nShift);
              mv[2].ver = mv[2].ver >= 0 ? (mv[2].ver + nOffset) >> nShift : -((-mv[2].ver + nOffset) >> nShift);

              if (currPU.interDir == 3)
              {
                featureCounter.biPredPel += numberOfPels;
              }
              else
              {
                featureCounter.uniPredPel += numberOfPels;
              }

              for (int iter = 0; iter < 3; iter++)
              {
                if (mv[iter].hor == 0 && mv[iter].ver == 0)
                {
                  featureCounter.affineCopyCUPel += numberOfPels;
                }
                if (mv[iter].hor != 0 && mv[iter].ver == 0)
                {
                  featureCounter.affineFracPelHor += numberOfPels;
                }
                if (mv[iter].hor == 0 && mv[iter].ver != 0)
                {
                  featureCounter.affineFracPelVer += numberOfPels;
                }
                if (mv[iter].hor != 0 && mv[iter].ver != 0)
                {
                  featureCounter.affineFracPelBoth += numberOfPels;
                }
              }
            }
            if (currPU.interDir != 1 /* PRED_L1 */)
            {
              Mv                mv[3];
              const CMotionBuf &mb = currPU.getMotionBuf();
              mv[0]                = mb.at(0, 0).mv[REF_PIC_LIST_1];
              mv[1]                = mb.at(mb.width - 1, 0).mv[REF_PIC_LIST_1];
              mv[2]                = mb.at(0, mb.height - 1).mv[REF_PIC_LIST_1];

              mv[0].hor = mv[0].hor >= 0 ? (mv[0].hor + nOffset) >> nShift : -((-mv[0].hor + nOffset) >> nShift);
              mv[0].ver = mv[0].ver >= 0 ? (mv[0].ver + nOffset) >> nShift : -((-mv[0].ver + nOffset) >> nShift);
              mv[1].hor = mv[1].hor >= 0 ? (mv[1].hor + nOffset) >> nShift : -((-mv[1].hor + nOffset) >> nShift);
              mv[1].ver = mv[1].ver >= 0 ? (mv[1].ver + nOffset) >> nShift : -((-mv[1].ver + nOffset) >> nShift);
              mv[2].hor = mv[2].hor >= 0 ? (mv[2].hor + nOffset) >> nShift : -((-mv[2].hor + nOffset) >> nShift);
              mv[2].ver = mv[2].ver >= 0 ? (mv[2].ver + nOffset) >> nShift : -((-mv[2].ver + nOffset) >> nShift);

              if (currPU.interDir != 3)
              {
                featureCounter.uniPredPel += numberOfPels;
              }

              for (int iter = 0; iter < 3; iter++)
              {
                if (mv[iter].hor == 0 && mv[iter].ver == 0)
                {
                  featureCounter.affineCopyCUPel += numberOfPels;
                }
                if (mv[iter].hor != 0 && mv[iter].ver == 0)
                {
                  featureCounter.affineFracPelHor += numberOfPels;
                }
                if (mv[iter].hor == 0 && mv[iter].ver != 0)
                {
                  featureCounter.affineFracPelVer += numberOfPels;
                }
                if (mv[iter].hor != 0 && mv[iter].ver != 0)
                {
                  featureCounter.affineFracPelBoth += numberOfPels;
                }
              }
            }
          }
        }
      }

      for (auto &currTU: CU::traverseTUs(currCU))
      {
        bool     m_notOnCounterGridWidth  = false;
        bool     m_notOnCounterGridHeight = false;
        SizeType currTUWIdx               = MAX_UINT;
        SizeType currTUHIdx               = MAX_UINT;
        currTUWIdx                        = floorLog2(currTU.Y().width);
        currTUHIdx                        = floorLog2(currTU.Y().height);
        if ((currTUWIdx <= MAX_CU_DEPTH + 1) && (currTUHIdx <= MAX_CU_DEPTH + 1))
        {
          if (currTU.Y().width != int(pow(2, currTUWIdx)))
          {
            m_notOnCounterGridWidth = true;
          }

          if (currTU.Y().height != int(pow(2, currTUHIdx)))
          {
            m_notOnCounterGridHeight = true;
          }

          if (currTU.cu->skip)
          {
            featureCounter.transformSkipBlocks[currTUWIdx][currTUHIdx]++;
            featureCounter.transformLumaSkipBlocks[currTUWIdx][currTUHIdx]++;
            if (m_notOnCounterGridHeight || m_notOnCounterGridWidth)
            {
              if (m_notOnCounterGridHeight)
              {
                featureCounter.transformSkipBlocks[currTUWIdx][currTUHIdx - 1]++;
                featureCounter.transformLumaSkipBlocks[currTUWIdx][currTUHIdx - 1]++;
              }

              if (m_notOnCounterGridWidth)
              {
                featureCounter.transformSkipBlocks[currTUWIdx - 1][currTUHIdx]++;
                featureCounter.transformLumaSkipBlocks[currTUWIdx - 1][currTUHIdx]++;
              }
            }
          }
          else
          {
            featureCounter.transformBlocks[currTUWIdx][currTUHIdx]++;
            featureCounter.transformLumaBlocks[currTUWIdx][currTUHIdx]++;
            if (m_notOnCounterGridHeight || m_notOnCounterGridWidth)
            {
              if (m_notOnCounterGridHeight)
              {
                featureCounter.transformBlocks[currTUWIdx][currTUHIdx - 1]++;
                featureCounter.transformLumaBlocks[currTUWIdx][currTUHIdx - 1]++;
              }

              if (m_notOnCounterGridWidth)
              {
                featureCounter.transformBlocks[currTUWIdx - 1][currTUHIdx]++;
                featureCounter.transformLumaBlocks[currTUWIdx - 1][currTUHIdx]++;
              }
            }
          }

          if (currTU.cu->lfnstIdx && currTU.mtsIdx[COMPONENT_Y] != MTS_SKIP
              && (currTU.cu->isSepTree() ? true : isLuma(COMPONENT_Y)))
          {
            bool significantCoeff = false;
            for (int bufferScan = 0; bufferScan < currTU.lwidth() * currTU.lheight() && !significantCoeff; bufferScan++)
            {
              if (currTU.getCoeffs(ComponentID(0)).buf[bufferScan] != 0)
              {
                significantCoeff = true;
              }
            }

            if (significantCoeff)
            {
              if (currTU.Y().width >= 8 && currTU.Y().height >= 8)
              {
                featureCounter.transformLFNST8++;
              }
              else
              {
                featureCounter.transformLFNST4++;
              }
            }
          }

          int maxNumberOfCoeffs = 0;

          maxNumberOfCoeffs = currTU.Y().width * currTU.Y().height;
          if (currTU.cbf[COMPONENT_Y] == 0)
          {
            maxNumberOfCoeffs = 0;
          }

          for (int i = 0; i < maxNumberOfCoeffs; i++)
          {
            int counterCoeff = currTU.getCoeffs(COMPONENT_Y).buf[i];
            if (counterCoeff != 0)
            {
              featureCounter.nrOfCoeff++;

              if (counterCoeff < 0)
              {
                counterCoeff *= -1;
              }

              if (counterCoeff > 1)
              {
                featureCounter.coeffG1++;
              }

              counterCoeff--;   // taking account of the fact that n_coeffg1 has already been counted
              double ldVal = counterCoeff < 2 ? 0.0
                                              : floorLog2(counterCoeff)
                                                  + (2 * counterCoeff >= (3 << floorLog2(counterCoeff)) ? 0.585 : 0.0);
              featureCounter.valueOfCoeff += ldVal;
            }
          }
        }
      }
    }
  }

  for (auto &currCU: cs.traverseCUs(ctuArea, CHANNEL_TYPE_CHROMA))
  {
    SizeType cuCbWidthIdx  = floorLog2(currCU.Cb().width);
    SizeType cuCbHeightIdx = floorLog2(currCU.Cb().height);
    SizeType cuCrWidthIdx  = floorLog2(currCU.Cr().width);
    SizeType cuCrHeightIdx = floorLog2(currCU.Cr().height);
    
    if ((cuCbWidthIdx <= MAX_CU_DEPTH+1) && (cuCbHeightIdx <= MAX_CU_DEPTH+1) && (cuCrWidthIdx <= MAX_CU_DEPTH+1) && (cuCrHeightIdx <= MAX_CU_DEPTH+1))
    {
      if (currCU.predMode == MODE_INTRA)   // Intra-Mode
      {
        for (auto &currPU: CU::traversePUs(currCU))
        {
          SizeType chromaWidthIdx  = floorLog2(currPU.Cb().width);
          SizeType chromaHeightIdx = floorLog2(currPU.Cb().height);
          if ((chromaWidthIdx <= MAX_CU_DEPTH+1) && (chromaHeightIdx <= MAX_CU_DEPTH+1))
          {
            featureCounter.intraBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
            SizeType chromaPredDir = currPU.intraDir[CHANNEL_TYPE_CHROMA];
            // Intra Prediction Direction
            if (currCU.mipFlag)
            {
              featureCounter.intraMIPBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              featureCounter.intraChromaMIPBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
            }
            else
            {
              if (chromaPredDir == PLANAR_IDX)
              {
                featureCounter.intraChromaPlaBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir == DC_IDX)
              {
                featureCounter.intraChromaDcBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir == HOR_IDX || chromaPredDir == VER_IDX || chromaPredDir == DIA_IDX)
              {
                featureCounter.intraChromaHvdBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir < LM_CHROMA_IDX)
              {
                featureCounter.intraChromaAngBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }

              if (PU::isLMCMode(chromaPredDir))
              {
                featureCounter.intraChromaCrossCompBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
            }

            bool tempPDPC =
              (currPU.Cb().width >= MIN_TB_SIZEY && currPU.Cb().height >= MIN_TB_SIZEY) && isLuma(COMPONENT_Cb)
                ? currPU.multiRefIdx
                : false;
            if (tempPDPC)
            {
              featureCounter.intraPDPCBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              featureCounter.intraChromaPDPCBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
            }
          }
        }

        if (currCU.ispMode > NOT_INTRA_SUBPARTITIONS)
        {
          if (currCU.ispMode == VER_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsVertical[cuCbWidthIdx][cuCbHeightIdx]++;
            featureCounter.intraChromaSubPartitionsVertical[cuCbWidthIdx][cuCbHeightIdx]++;
          }
          if (currCU.ispMode == HOR_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsHorizontal[cuCbWidthIdx][cuCbHeightIdx]++;
            featureCounter.intraChromaSubPartitionsHorizontal[cuCbWidthIdx][cuCbHeightIdx]++;
          }
        }
      }

      if (currCU.predMode == MODE_INTRA)   // Intra-Mode
      {
        for (auto &currPU: CU::traversePUs(currCU))
        {
          SizeType chromaWidthIdx  = floorLog2(currPU.Cr().width);
          SizeType chromaHeightIdx = floorLog2(currPU.Cr().height);
          featureCounter.intraBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
          SizeType chromaPredDir = currPU.intraDir[CHANNEL_TYPE_CHROMA];
          if ((chromaWidthIdx <= MAX_CU_DEPTH + 1) && (chromaHeightIdx <= MAX_CU_DEPTH + 1))
          {
            if (currCU.mipFlag)
            {
              featureCounter.intraMIPBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              featureCounter.intraChromaMIPBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
            }
            else
            {
              if (chromaPredDir == PLANAR_IDX)
              {
                featureCounter.intraChromaPlaBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir == DC_IDX)
              {
                featureCounter.intraChromaDcBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir == HOR_IDX || chromaPredDir == VER_IDX || chromaPredDir == DIA_IDX)
              {
                featureCounter.intraChromaHvdBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
              else if (chromaPredDir < NUM_CHROMA_MODE)
              {
                featureCounter.intraChromaAngBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }

              if (PU::isLMCMode(chromaPredDir))
              {
                featureCounter.intraChromaCrossCompBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              }
            }
            bool tempPDPC =
              (currPU.Cr().width >= MIN_TB_SIZEY && currPU.Cr().height >= MIN_TB_SIZEY) && isLuma(COMPONENT_Cr)
                ? currPU.multiRefIdx
                : false;
            if (tempPDPC)
            {
              featureCounter.intraPDPCBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
              featureCounter.intraChromaPDPCBlockSizes[chromaWidthIdx][chromaHeightIdx]++;
            }
          }
        }

        if (currCU.ispMode > NOT_INTRA_SUBPARTITIONS)
        {
          if (currCU.ispMode == VER_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsVertical[cuCrWidthIdx][cuCrHeightIdx]++;
            featureCounter.intraChromaSubPartitionsVertical[cuCrWidthIdx][cuCrHeightIdx]++;
          }
          if (currCU.ispMode == HOR_INTRA_SUBPARTITIONS)
          {
            featureCounter.intraSubPartitionsHorizontal[cuCrWidthIdx][cuCrHeightIdx]++;
            featureCounter.intraSubPartitionsHorizontal[cuCrWidthIdx][cuCrHeightIdx]++;
          }
        }
      }


      for (auto &currTU: CU::traverseTUs(currCU))
      {
        for (int m_compID = 1; m_compID < getNumberValidComponents(currTU.chromaFormat); m_compID++)
        {
          bool     m_notOnCounterGridWidth  = false;
          bool     m_notOnCounterGridHeight = false;
          SizeType currTUWIdx               = MAX_UINT;
          SizeType currTUHIdx               = MAX_UINT;
          if ((currTUWIdx <= MAX_CU_DEPTH+1) && (currTUHIdx <= MAX_CU_DEPTH+1))
          {
            if (m_compID == COMPONENT_Cb)
            {
              currTUWIdx = floorLog2(currTU.Cb().width);
              currTUHIdx = floorLog2(currTU.Cb().height);
              if (currTU.Cb().width != int(pow(2, currTUWIdx)))
              {
                m_notOnCounterGridWidth = true;
              }

              if (currTU.Cb().height != int(pow(2, currTUHIdx)))
              {
                m_notOnCounterGridHeight = true;
              }
            }
            else
            {
              currTUWIdx = floorLog2(currTU.Cr().width);
              currTUHIdx = floorLog2(currTU.Cr().height);
              if (currTU.Cr().width != int(pow(2, currTUWIdx)))
              {
                m_notOnCounterGridWidth = true;
              }

              if (currTU.Cr().height != int(pow(2, currTUHIdx)))
              {
                m_notOnCounterGridHeight = true;
              }
            }

            if (currTU.cu->skip)
            {
              featureCounter.transformSkipBlocks[currTUWIdx][currTUHIdx]++;
              featureCounter.transformChromaSkipBlocks[currTUWIdx][currTUHIdx]++;
              if (m_notOnCounterGridHeight || m_notOnCounterGridWidth)
              {
                if (m_notOnCounterGridHeight)
                {
                  featureCounter.transformSkipBlocks[currTUWIdx][currTUHIdx - 1]++;
                  featureCounter.transformChromaSkipBlocks[currTUWIdx][currTUHIdx - 1]++;
                }

                if (m_notOnCounterGridWidth)
                {
                  featureCounter.transformSkipBlocks[currTUWIdx - 1][currTUHIdx]++;
                  featureCounter.transformChromaSkipBlocks[currTUWIdx - 1][currTUHIdx]++;
                }
              }
            }
            else
            {
              featureCounter.transformBlocks[currTUWIdx][currTUHIdx]++;
              featureCounter.transformChromaBlocks[currTUWIdx][currTUHIdx]++;
              if (m_notOnCounterGridHeight || m_notOnCounterGridWidth)
              {
                if (m_notOnCounterGridHeight)
                {
                  featureCounter.transformBlocks[currTUWIdx][currTUHIdx - 1]++;
                  featureCounter.transformChromaBlocks[currTUWIdx][currTUHIdx - 1]++;
                }

                if (m_notOnCounterGridWidth)
                {
                  featureCounter.transformBlocks[currTUWIdx - 1][currTUHIdx]++;
                  featureCounter.transformChromaBlocks[currTUWIdx - 1][currTUHIdx]++;
                }
              }
            }

            if (currTU.cu->lfnstIdx && currTU.mtsIdx[m_compID] != MTS_SKIP
                && (currTU.cu->isSepTree() ? true : isLuma(COMPONENT_Cr)))
            {
              bool significantCoeff = false;
              if (m_compID == COMPONENT_Cb)
              {
                for (int bufferScan = 0;
                     bufferScan < currTU.Cb().width * currTU.Cb().height && significantCoeff == false; bufferScan++)
                {
                  if (currTU.getCoeffs(ComponentID(m_compID)).buf[bufferScan] != 0)
                  {
                    significantCoeff = true;
                  }
                }
              }
              else
              {
                for (int bufferScan = 0;
                     bufferScan < currTU.Cr().width * currTU.Cr().height && significantCoeff == false; bufferScan++)
                {
                  if (currTU.getCoeffs(ComponentID(m_compID)).buf[bufferScan] != 0)
                  {
                    significantCoeff = true;
                  }
                }
              }

              if (significantCoeff)
              {
                if (currTU.Cb().width >= 8 && currTU.Cb().height >= 8)
                {
                  featureCounter.transformLFNST8++;
                }
                else
                {
                  featureCounter.transformLFNST4++;
                }
              }
            }

            int maxNumberOfCoeffs = 0;
            if (m_compID == COMPONENT_Y)
            {
              maxNumberOfCoeffs = currTU.Y().width * currTU.Y().height;
              if (currTU.cbf[COMPONENT_Y] == 0)
              {
                maxNumberOfCoeffs = 0;
              }
            }
            else if (m_compID == COMPONENT_Cb)
            {
              maxNumberOfCoeffs = currTU.Cb().width * currTU.Cb().height;
              if (currTU.cbf[COMPONENT_Cb] == 0)
              {
                maxNumberOfCoeffs = 0;
              }
            }
            else
            {
              maxNumberOfCoeffs = currTU.Cr().width * currTU.Cr().height;
              if (currTU.cbf[COMPONENT_Cr] == 0)
              {
                maxNumberOfCoeffs = 0;
              }
            }

            for (int i = 0; i < maxNumberOfCoeffs; i++)
            {
              int counterCoeff = currTU.getCoeffs((ComponentID) m_compID).buf[i];

              if (counterCoeff != 0)
              {
                featureCounter.nrOfCoeff++;

                if (counterCoeff < 0)   // abs val
                {
                  counterCoeff *= -1;
                }

                if (counterCoeff > 1)
                {
                  featureCounter.coeffG1++;
                }

                counterCoeff--;   // taking account of the fact that n_coeffg1 has already been counted
                double ldVal =
                  counterCoeff < 2
                    ? 0.0
                    : floorLog2(counterCoeff) + (2 * counterCoeff >= (3 << floorLog2(counterCoeff)) ? 0.585 : 0.0);
                featureCounter.valueOfCoeff += ldVal;
              }
            }
          }
        }
      }
    }
  }
}
#endif
