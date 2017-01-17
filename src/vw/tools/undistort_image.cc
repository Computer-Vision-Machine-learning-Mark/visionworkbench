// __BEGIN_LICENSE__
//  Copyright (c) 2006-2013, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NASA Vision Workbench is licensed under the Apache License,
//  Version 2.0 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__

/// Tool to undistort a pinhole camera image given the camera model file.


#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4996)
#endif

#include <vw/Core/Exception.h>
#include <vw/Image/ImageView.h>
#include <vw/Image/PixelTypes.h>
#include <vw/Image/Interpolation.h>
#include <vw/FileIO/DiskImageResource.h>
#include <vw/Camera/PinholeModel.h>
#include <vw/Camera/LensDistortion.h>
#include <vw/FileIO/FileUtils.h>
#include <vw/tools/Common.h>
#include <vw/Cartography/GeoReferenceUtils.h>

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

using namespace vw;
using vw::camera::PinholeModel;
using vw::camera::LensDistortion;

// Global variables, to make it easier to invoke the function do_work
// with many channels and channel types.
std::string input_file_name, output_file_name, camera_file_name;


template <class ImageT>
class UndistortView: public ImageViewBase< UndistortView<ImageT> >{
  ImageT m_dist_img;
  int m_cols, m_rows;
  Vector2 m_offset;
  PinholeModel m_camera_model;

  typedef typename ImageT::pixel_type PixelT;

public:
  UndistortView(ImageT const& dist_img, int cols, int rows,
                Vector2 const& offset, 
                PinholeModel const& camera_model):
    m_dist_img(dist_img), m_cols(cols), m_rows(rows),
    m_offset(offset), m_camera_model(camera_model){}
  
  typedef PixelT pixel_type;
  typedef PixelT result_type;
  typedef ProceduralPixelAccessor<UndistortView> pixel_accessor;

  inline int32 cols() const { return m_cols; }
  inline int32 rows() const { return m_rows; }
  inline int32 planes() const { return 1; }

  inline pixel_accessor origin() const { return pixel_accessor( *this, 0, 0 ); }

  inline pixel_type operator()( double/*i*/, double/*j*/, int32/*p*/ = 0 ) const {
    vw_throw(NoImplErr() << "UndistortView::operator()(...) is not implemented");
    return pixel_type();
  }

  typedef CropView<ImageView<pixel_type> > prerasterize_type;
  inline prerasterize_type prerasterize(BBox2i const& bbox) const {

    InterpolationView<EdgeExtensionView<ImageT, ZeroEdgeExtension>, BilinearInterpolation>
      interp_dist_img = interpolate(m_dist_img, BilinearInterpolation(), ZeroEdgeExtension());

    const LensDistortion* lens_ptr = m_camera_model.lens_distortion();
    const double pitch = m_camera_model.pixel_pitch();

    ImageView<result_type> tile(bbox.width(), bbox.height());

    for (int col = bbox.min().x(); col < bbox.max().x(); col++){
      for (int row = bbox.min().y(); row < bbox.max().y(); row++){

        Vector2 lens_loc = elem_prod(Vector2(col, row) + m_offset, pitch);
        Vector2 out_loc  = lens_ptr->distorted_coordinates(m_camera_model, lens_loc);
        Vector2 in_loc = elem_quot(out_loc, pitch);

        tile(col - bbox.min().x(), row - bbox.min().y() )
          = interp_dist_img(in_loc[0], in_loc[1]);
      }
    }
    
    return prerasterize_type(tile, -bbox.min().x(), -bbox.min().y(),
                             cols(), rows() );
  }

  template <class DestT>
  inline void rasterize(DestT const& dest, BBox2i bbox) const {
    vw::rasterize(prerasterize(bbox), dest, bbox);
  }
};
template <class ImageT>
UndistortView<ImageT> undistort_image(ImageT const& dist_img,
                                      int cols, int rows,
                                      Vector2 const& offset, 
                                      PinholeModel const& camera_model){
  return UndistortView<ImageT>(dist_img, cols, rows, offset, camera_model);
}


/// Generates an undistorted view of an input image.
/// - This should be moved somewhere else.
/// - Could easily add the option to keep the image the same size.
template <class PixelT>
void do_work() {

  // Load the input image
  vw_out() << "Loading input image: " << input_file_name << "\n";
  ImageView<PixelT> dist_img;
  read_image(dist_img, input_file_name);

  // Load the camera
  vw_out() << "Loading camera model file: " << camera_file_name << "\n";
  PinholeModel camera_model(camera_file_name);
  
  const int width_in  = dist_img.cols();
  const int height_in = dist_img.rows();
  const LensDistortion* lens_ptr = camera_model.lens_distortion();
  const double pitch = camera_model.pixel_pitch();

  // Figure out the size of the undistorted image
  // - Iterate along each side of the image and record where the output pixels go
  Vector2 out_loc, lens_loc;
  BBox2   output_area;
  for (int r=0; r<height_in; ++r) {
    lens_loc = elem_prod(Vector2(0, r), pitch);
    out_loc  = lens_ptr->undistorted_coordinates(camera_model, lens_loc);
    output_area.grow(elem_quot(out_loc, pitch));
    
    lens_loc = elem_prod(Vector2(width_in-1, r), pitch);
    out_loc  = lens_ptr->undistorted_coordinates(camera_model, lens_loc);
    output_area.grow(elem_quot(out_loc, pitch));
  }
  for (int c=0; c<width_in; ++c) {
    lens_loc = elem_prod(Vector2(c, 0), pitch);
    out_loc  = lens_ptr->undistorted_coordinates(camera_model, lens_loc);
    output_area.grow(elem_quot(out_loc, pitch));
    
    lens_loc = elem_prod(Vector2(c, height_in-1), pitch);
    out_loc  = lens_ptr->undistorted_coordinates(camera_model, lens_loc);
    output_area.grow(elem_quot(out_loc, pitch));
  }

  Vector2 offset = output_area.min();

  int cols = floor(output_area.width());
  int rows = floor(output_area.height());
  vw_out() << "Output image size: " << cols << ' ' << rows << std::endl;

  vw::cartography::GdalWriteOptions write_options;

  bool has_georef = false;
  cartography::GeoReference georef;
  double nodata = 0;
  bool has_nodata = false;  // TODO: May need to set this to true, at least for grayscale images
  vw_out() << "Writing: " << output_file_name << std::endl;
  TerminalProgressCallback tpc("vw", "");
  block_write_gdal_image(output_file_name,

                         undistort_image(dist_img,  
                                         cols, rows,  
                                         offset,  
                                         camera_model),
                         has_georef,  
                         georef, has_nodata, nodata,  
                         write_options, tpc);

  // Save the camera model for the undistorted image
  PinholeModel out_model = vw::camera::strip_lens_distortion(camera_model);
  out_model.set_point_offset(out_model.point_offset() - offset*out_model.pixel_pitch());
  
  std::string out_cam_path = fs::path(output_file_name).replace_extension(".tsai").string();
  vw_out() << "Writing: " << out_cam_path << std::endl;
  out_model.write(out_cam_path);
  
  vw_out() << "Finished!\n";
  
} // End function do_work

// Magic to make do_work() work with many number of channels and channel types
#define DO_WORK(PIXELTYPEMACRO, CHANNELTYPE) DO_WORK_(PIXELTYPEMACRO, CHANNELTYPE)
#define DO_WORK_(PIXELTYPEMACRO, CHANNELTYPE) DO_WORK__(PIXELTYPEMACRO, CHANNELTYPE, PIXELTYPEMACRO ## _ ## CHANNELTYPE)
#define DO_WORK__(PIXELTYPEMACRO, CHANNELTYPE, FUNCSUFFIX) \
  void do_work_##FUNCSUFFIX(void) {                                    \
    do_work<PIXELTYPEMACRO<CHANNELTYPE > >();                          \
  }

// The channel type can be integer or float
#define DO_WORK_ALL_CHANNELS( PIXELTYPE )  \
  DO_WORK( PIXELTYPE, uint8 );             \
  DO_WORK( PIXELTYPE, int8 );              \
  DO_WORK( PIXELTYPE, uint16 );            \
  DO_WORK( PIXELTYPE, int16 );             \
  DO_WORK( PIXELTYPE, float32 );           \
  DO_WORK( PIXELTYPE, float64 ); 

// Support 1 or 3 channel images, perhaps with an alpha channel
DO_WORK_ALL_CHANNELS(PixelGray)
DO_WORK_ALL_CHANNELS(PixelGrayA)
DO_WORK_ALL_CHANNELS(PixelRGB)
DO_WORK_ALL_CHANNELS(PixelRGBA)

#define SWITCH_ON_CHANNEL_TYPE( PIXELTYPE )                            \
  switch (fmt.channel_type) {                                          \
  case VW_CHANNEL_UINT8:   do_work_##PIXELTYPE##_uint8();   break;     \
  case VW_CHANNEL_INT8:    do_work_##PIXELTYPE##_int8();    break;     \
  case VW_CHANNEL_UINT16:  do_work_##PIXELTYPE##_uint16();  break;     \
  case VW_CHANNEL_INT16:   do_work_##PIXELTYPE##_int16();   break;     \
  case VW_CHANNEL_FLOAT32: do_work_##PIXELTYPE##_float32(); break;     \
  default:                 do_work_##PIXELTYPE##_float64(); break;     \
  }

void do_work_all_channels(std::string const& input_file_name){

  ImageFormat fmt = vw::image_format(input_file_name);
  try {
    switch (fmt.pixel_format) {
    case VW_PIXEL_GRAY:  SWITCH_ON_CHANNEL_TYPE(PixelGray);  break; // 1 channel
    case VW_PIXEL_GRAYA: SWITCH_ON_CHANNEL_TYPE(PixelGrayA); break; // 1 channel  + alpha channel
    case VW_PIXEL_RGB:   SWITCH_ON_CHANNEL_TYPE(PixelRGB);   break; // 3 channels 
    default:             SWITCH_ON_CHANNEL_TYPE(PixelRGBA);  break; // 3 channels + alpha channel
    }
  }catch (const Exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}
    
int main( int argc, char *argv[] ) {

  // TODO: Output better be float?
  
  po::options_description desc("Usage: undistort_image [options] <input image> <camera model> \n\nOptions");
  desc.add_options()
    ("help,h",        "Display this help message")
    ("input-file",    po::value<std::string>(&input_file_name), 
                      "Explicitly specify the input file")
    ("camera-file",    po::value<std::string>(&camera_file_name), 
                      "Explicitly specify the camera file")
    ("output-file,o", po::value<std::string>(&output_file_name)->default_value("output.png"), 
                      "Specify the output file");
  po::positional_options_description p;
  p.add("input-file", 1);
  p.add("camera-file", 1);

  po::variables_map vm;
  try {
    po::store( po::command_line_parser( argc, argv ).options(desc).positional(p).run(), vm );
    po::notify( vm );
  } catch (const po::error& e) {
    vw_out() << "An error occured while parsing command line arguments.\n";
    vw_out() << "\t" << e.what() << "\n\n";
    vw_out() << desc;
    return 1;
  }

  if( vm.count("help") ) {
    vw_out() << desc << std::endl;
    return 1;
  }
  if( (vm.count("input-file") != 1) || (vm.count("camera-file") != 1) ) {
    vw_out() << "Error: Must specify exactly one image file and one camera file!" << std::endl;
    vw_out() << desc << std::endl;
    return 1;
  }

  vw::create_out_dir(output_file_name);
  do_work_all_channels(input_file_name);
  
  return 0;
}
