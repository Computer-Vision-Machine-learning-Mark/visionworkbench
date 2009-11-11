// __BEGIN_LICENSE__
// Copyright (C) 2006-2009 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__

#ifndef __VW_GUI_TILEGENERATOR_H__
#define __VW_GUI_TILEGENERATOR_H__

#include <vw/Core/FundamentalTypes.h>
#include <vw/Math/BBox.h>
#include <vw/Image/ImageView.h>
#include <vw/Image/PixelTypes.h>
#include <vw/Image/ViewImageResource.h>
#include <vw/Plate/PlateFile.h>

namespace vw {
namespace gui {

  struct TileLocator {
    int col;
    int row;
    int level;

    bool is_valid() const {
      return (col >= 0 && row >= 0 && col < pow(2, level) && row < pow(2, level));
    }
  };

  BBox2i tile_to_bbox(int32 tile_size, int col, int row, int level, int max_level);
  
  std::list<TileLocator> bbox_to_tiles(int32 tile_size, BBox2i bbox, int level, int max_level);

  // --------------------------------------------------------------------------
  //                              TILE GENERATOR
  // --------------------------------------------------------------------------

  class TileGenerator {
  public:
    virtual ~TileGenerator() {}
    virtual boost::shared_ptr<ViewImageResource> generate_tile(TileLocator const& tile_info) = 0;

    virtual int cols() const = 0;
    virtual int rows() const = 0;
    virtual PixelFormatEnum pixel_format() const = 0;
    virtual ChannelTypeEnum channel_type() const = 0;
    virtual int tile_size() const = 0;
    
    // Use this method to generate the correct type of TileGenerator
    // for a given filename.
    static boost::shared_ptr<TileGenerator> create(std::string filename);
  };

  // --------------------------------------------------------------------------
  //                         TEST PATTERN TILE GENERATOR
  // --------------------------------------------------------------------------

  class TestPatternTileGenerator : public TileGenerator {
    int m_tile_size;

  public:
    TestPatternTileGenerator(int tile_size) : m_tile_size(tile_size) {}
    virtual ~TestPatternTileGenerator() {}

    virtual boost::shared_ptr<ViewImageResource> generate_tile(TileLocator const& tile_info);

    virtual int cols() const;
    virtual int rows() const;
    virtual PixelFormatEnum pixel_format() const;
    virtual ChannelTypeEnum channel_type() const;
    virtual int tile_size() const;
  };

  // --------------------------------------------------------------------------
  //                         PLATE FILE TILE GENERATOR
  // --------------------------------------------------------------------------

  class PlatefileTileGenerator : public TileGenerator {
    boost::shared_ptr<vw::platefile::PlateFile> m_platefile;

  public:
    PlatefileTileGenerator(std::string platefile_name);
    virtual ~PlatefileTileGenerator() {}

    virtual boost::shared_ptr<ViewImageResource> generate_tile(TileLocator const& tile_info);

    virtual int cols() const;
    virtual int rows() const;
    virtual PixelFormatEnum pixel_format() const;
    virtual ChannelTypeEnum channel_type() const;
    virtual int tile_size() const;
  };

  // --------------------------------------------------------------------------
  //                             IMAGE TILE GENERATOR
  // --------------------------------------------------------------------------

  class ImageTileGenerator : public TileGenerator {
    std::string m_filename;

  public:
    ImageTileGenerator(std::string filename) : m_filename(filename) {}
    virtual ~ImageTileGenerator() {}

    virtual boost::shared_ptr<ViewImageResource> generate_tile(TileLocator const& tile_info);

    virtual int cols() const;
    virtual int rows() const;
    virtual PixelFormatEnum pixel_format() const;
    virtual ChannelTypeEnum channel_type() const;
    virtual int tile_size() const;
  };


}} // namespace vw::gui

#endif // __VW_GUI_TILEGENERATOR_H__

