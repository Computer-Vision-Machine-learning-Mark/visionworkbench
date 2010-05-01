// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file ba_test.cc
///

/* {{{ Documentation
 *
 *  PROGRAM:  ba_test
 *
 *  AUTHOR:   Michael Styer <michael@styer.net>
 *
 *  DATE:     9/16/2009
 *
 *  SUMMARY:
 *
 *    This program is part of the bundle adjustment test harness written in
 *    Summer 2009. The other components are: make_ba_test_data and
 *    run_ba_tests.
 *
 *    ba_test runs one full bundle adjustment procedure, as configured on the
 *    command line and/or in the specified configuration file. For a complete
 *    list of options and their default values, run 'ba_test -?' (or --help).
 *    Options that require specific explanation or notes are discussed below.
 *
 *    The configuration file syntax is simply <option>=<value>, one option per
 *    line, where <option> is the long name of the option.
 *
 *    At a minimum, you must provide a control network file via --cnet or -c,
 *    and a set of Pinhole (.tsai) camera model files as arguments on the
 *    command line. By default, ba_test will run the reference implementation
 *    of bundle adjustment up to 30 iterations, reading the required data from
 *    and writing its results to the current directory.
 *
 *  OPTIONS:
 *
 *    The -T option (--use-ba-type-dirs) causes ba_test to create a
 *    subdirectory of the specified data directory named for the specified
 *    type of bundle adjustment, and store its results in that directory. This
 *    allows you to run multiple tests of bundle adjustment for different
 *    bundle adjustment implementations on the same data set. Note that this
 *    is NOT used by the current test harness, however.
 *
 *    The -R option (--results-dir) allows you to specify an arbitrary
 *    directory ba_test should use for writing its results. The current test
 *    harness uses this option to control the location of the output from
 *    bundle adjustment.
 *  
 *    The -M option (--remove-outliers) causes ba_test to run two bundle
 *    adjustment procedures. After the first bundle adjustment, it will run
 *    cnet_editor on the resulting control network, removing any control
 *    measures for which the calculated error is greater than the specified
 *    number of standard deviations (2 by default) away from the mean error.
 *    Then it runs a second bundle adjustment using the new control network
 *    and reports the results from this adjustment procedure.
 *
 *    The -r option (--report-level) controls the amount of information
 *    generated by BundleAjustReport. To use outlier removal, this option must
 *    be set to at least 35 so that the image_mean.err file is generated.
 *
 *  OUTPUT:
 *
 *    ba_test generates several output files.
 *
 *      <ba_type>.bvis, <ba_type>.report: 
 *        These are generated by the BundleAdjustReport object and
 *        are not used by this program or the test harness.
 *
 *      image_mean.err:
 *        This is also generated by the BundleAdjustReporter. It is used by
 *        cnet_editor when removing outliers to determine which control
 *        measures to remove from the control network.
 *
 *      [cam|wp]_initial.txt, [cam|wp]_final.txt:
 *        At the beginning of a run, ba_test writes out the initial values of
 *        the camera parameters and world points it receives to
 *        cam_initial.txt or wp_initial.txt as appropriate. After the run has
 *        completed, it writes out the final values of camera praameters and
 *        world points to the corresponding *_final.txt file. These are used
 *        by the test harness to generate error evaluation statistics.
 *
 *      *.adjust:
 *        A text-format camera model file is generated for each input camera
 *        model with the final adjusted camera parameters. Each file has the
 *        same base name as the input camera model file with the extension
 *        '.adjust'.
 *
 * }}} */

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/version.hpp>
namespace po = boost::program_options;
namespace fs = boost::filesystem;

#include <vw/Camera/CAHVORModel.h>
#include <vw/Camera/PinholeModel.h>
#include <vw/BundleAdjustment.h>
#include <vw/Math.h>

using namespace vw;
using namespace vw::camera;
using namespace vw::ba;

#include <cstdlib>
#include <iostream>
#include <iomanip>

typedef std::vector<boost::shared_ptr<CameraModel> > CameraVector;

const fs::path ConfigFileDefault      = "ba_test.cfg";
const fs::path CameraParamsReportFile = "iterCameraParam.txt";
const fs::path PointsReportFile       = "iterPointsParam.txt";
const std::string CnetEditor          = "cnet_editor";
const fs::path MeanErrorsFile         = "image_mean.err";
const fs::path ProcessedCnetFile      = "processed.cnet";

using std::cout;
using std::endl;

#if VW_BOOST_VERSION < 103400
std::ostream& operator<<(std::ostream& os, const fs::path& p) {
    os << p.string();
    return os;
}

std::istream& operator>>(std::istream& is, fs::path& p) {
    std::string s;
    is >> s;
    p = s;
    return is;
}
#endif

/*
 * Program Options
 */
/* {{{ ProgramOptions */
enum BundleAdjustmentT { 
    REF, 
    SPARSE, 
    ROBUST_REF, 
    ROBUST_SPARSE,
    SPARSE_HUBER, 
    SPARSE_CAUCHY
};

struct ProgramOptions {
  BundleAdjustmentT bundle_adjustment_type;
  double lambda;
  int control;
  double huber_param;
  double cauchy_param;
  double camera_position_sigma; // constraint on adjustment to camera position
  double camera_pose_sigma;     // constraint on adjustment to camera pose
  double gcp_sigma;           // constraint on adjustment to GCP position
  double outlier_sd_cutoff;
  bool use_user_lambda;
  bool use_ba_type_dirs;
  bool save_iteration_data;
  bool remove_outliers;
  int min_matches;
  int report_level;
  int max_iterations;
  std::vector<fs::path> camera_files;
  fs::path cnet_file;
  fs::path data_dir;
  fs::path results_dir;
  std::string config_file;
  friend std::ostream& operator<<(std::ostream& ostr, const ProgramOptions& o);
};

/* {{{ operator<< */
std::ostream& operator<<(std::ostream& ostr, const ProgramOptions& o) {
  ostr << endl << "Configured Options (read from " << o.config_file << ")" << endl;
  ostr << "----------------------------------------------------" << endl;
  ostr << "Control network file: " << o.cnet_file << endl;
  ostr << "Bundle adjustment type: ";
  switch (o.bundle_adjustment_type) {
    case REF:
      ostr << "Reference"; break;
    case SPARSE:
      ostr << "Sparse"; break;
    case SPARSE_HUBER:
      ostr << "Sparse Huber"; break;
    case SPARSE_CAUCHY:
      ostr << "Sparse Cauchy"; break;
    case ROBUST_REF:
      ostr << "Robust Reference"; break;
    case ROBUST_SPARSE:
      ostr << "Robust Sparse"; break;
    default:
      ostr << "unrecognized type";
  }
  ostr << endl;
  if (o.use_user_lambda == true)
    ostr << "Lambda: " << o.lambda << endl;
  ostr << "Huber parameter: " << o.huber_param << endl;
  ostr << "Cauchy parameter: " << o.cauchy_param << endl;
  ostr << "Camera position sigma: " << o.camera_position_sigma << endl;
  ostr << "Camera pose sigma: " << o.camera_pose_sigma << endl;
  ostr << "Ground control point sigma: " << o.gcp_sigma << endl;
  ostr << "Minimum matches: " << o.min_matches << endl;
  ostr << "Maximum iterations: " << o.max_iterations << endl;
  ostr << "Save iteration data? " << std::boolalpha << o.save_iteration_data << endl;
  ostr << "Report level: " << o.report_level << endl;
  ostr << "Data directory: " << o.data_dir << endl;
  ostr << "Results directory: " << o.results_dir << endl;
  ostr << "Use bundle adjustment type dirs? " << std::boolalpha << o.use_ba_type_dirs << endl;
  ostr << "Remove outliers? " << std::boolalpha << o.remove_outliers << endl;
  ostr << "Outlier SD cutoff: " << o.outlier_sd_cutoff << endl;
  return ostr;
}
/* }}} operator<< */

/* {{{ string_to_ba_type */
BundleAdjustmentT string_to_ba_type(std::string &s) {
  BundleAdjustmentT t = REF;
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  if (s == "sparse") t = SPARSE;
  else if (s == "sparse_huber") t = SPARSE_HUBER;
  else if (s == "sparse_cauchy") t = SPARSE_CAUCHY;
  else if (s == "robust_ref") t = ROBUST_REF;
  else if (s == "robust_sparse") t = ROBUST_SPARSE;
  return t;
}
/* }}} */

/* {{{ ba_type_to_string */
std::string ba_type_to_string(BundleAdjustmentT t) {
  std::string s;
  switch (t) {
    case REF:
      s = "ref"; break;
    case SPARSE:
      s = "sparse"; break;
    case SPARSE_HUBER:
      s = "sparse_huber"; break;
    case SPARSE_CAUCHY:
      s = "sparse_cauchy"; break;
    case ROBUST_REF:
      s = "robust_ref"; break;
    case ROBUST_SPARSE:
      s = "robust_sparse"; break;
  }
  return s;
}
/* }}} */

/* }}} ProgramOptions */

/*
 * Function Definitions
 */
/* {{{ parse_options */
ProgramOptions parse_options(int argc, char* argv[]) {
  ProgramOptions opts;

  std::string data_dir_tmp;

  // Generic Options (generic_options)
  po::options_description generic_options("Options");
  generic_options.add_options()
    ("help,?", "Display this help message")
    ("verbose,v", "Verbose output")
    ("debug,d", "Debugging output")
    ("report-level,r",po::value<int>(&opts.report_level)->default_value(35),"Changes the detail of the Bundle Adjustment Report")
    ("config-file,f", po::value<std::string>(&opts.config_file)->default_value(ConfigFileDefault.string()),
        "File containing configuration options (if not given, defaults to reading ba_test.cfg in the current directory")
    ("print-config","Print configuration options and exit");

  std::string ba_type;
  // Bundle adjustment options (ba_options)
  po::options_description ba_options("Bundle Adjustment Configuration");
  ba_options.add_options()
    ("bundle-adjustment-type,b",
        po::value<std::string>(&ba_type)->default_value("ref"),
        "Select bundle adjustment type (options are: \"ref\", \"sparse\", \"sparse_huber\", \"sparse_cauchy\", \"robust_ref\", \"robust_sparse\" )")
    ("cnet,c",
        po::value<fs::path>(&opts.cnet_file),
        "Load a control network from a file")
    ("lambda,l",
        po::value<double>(&opts.lambda),
        "Set the initial value of the LM parameter lambda")
    ("control",
        po::value<int>(&opts.control)->default_value(0),
        "Control variable (see set_control in BundleAdjustmentBase.h)")
    ("huber-param",
        po::value<double>(&opts.huber_param),
        "Set value of Huber parameter")
    ("cauchy-param",
        po::value<double>(&opts.cauchy_param),
        "Set value of Cauchy parameter")
    ("camera-position-sigma",
        po::value<double>(&opts.camera_position_sigma)->default_value(1.0),
        "Covariance constraint on camera position")
    ("camera-pose-sigma",
        po::value<double>(&opts.camera_pose_sigma)->default_value(1e-16),
        "Covariance constraint on camera pose")
    ("gcp-sigma",
        po::value<double>(&opts.gcp_sigma)->default_value(1e-16),
        "Covariance constraint on ground control points")
    ("save-iteration-data,s",
        po::bool_switch(&opts.save_iteration_data),
        "Saves all camera information between iterations to <results-dir>/iterCameraParam.txt and saves point locations for all iterations in iterPointsParam.txt.")
    ("max-iterations,i",
        po::value<int>(&opts.max_iterations)->default_value(30),
        "Set the maximum number of iterations to run bundle adjustment.")
    ("min-matches,m",
        po::value<int>(&opts.min_matches)->default_value(30),
        "Set the minimum  number of matches between images that will be considered.")
    ("data-dir,D", po::value<std::string>(&data_dir_tmp)->default_value("."),
        "Directory to read input data from")
    ("results-dir,R", po::value<fs::path>(&opts.results_dir),
        "Directory to write output data to (if not present, defaults to 'data-dir')")
    ("use-ba-type-dirs,T",
        po::bool_switch(&opts.use_ba_type_dirs),
        "Store results in subdirectories of results-dir by bundle adjustment type")
    ("remove-outliers,M",
        po::bool_switch(&opts.remove_outliers),
        "Remove outliers using naive heuristic")
    ("outlier-sd-cutoff",
        po::value<double>(&opts.outlier_sd_cutoff)->default_value(2),
        "Remove outliers more than this number of std devs from the mean (implies -M)");

  // Hidden options, aka command line arguments (hidden_options)
  po::options_description hidden_options("");
  hidden_options.add_options()
    ("input-files", po::value<std::vector<fs::path> >(&opts.camera_files));

  // Ignored options (used by make_ba_test_data and not by this program)
  // We need to enumerate these because earlier boost versions can't use
  // allow_unregistered().
  // NB: That means that if you add an option to make_ba_test_data, you must
  // add it here too.
  po::options_description ignored_options("");
  ignored_options.add_options()
    ("pixel-inlier-noise-type","")
    ("pixel-inlier-df","")
    ("pixel-inlier-sigma","")
    ("pixel-outlier-noise-type","")
    ("pixel-outlier-df","")
    ("pixel-outlier-sigma","")
    ("pixel-outlier-freq","")
    ("xyz-inlier-noise-type","")
    ("xyz-inlier-df","")
    ("xyz-inlier-sigma","")
    ("xyz-outlier-noise-type","")
    ("xyz-outlier-df","")
    ("xyz-outlier-sigma","")
    ("xyz-outlier-freq","")
    ("euler-inlier-noise-type","")
    ("euler-inlier-df","")
    ("euler-inlier-sigma","")
    ("euler-outlier-noise-type","")
    ("euler-outlier-df","")
    ("euler-outlier-sigma","")
    ("euler-outlier-freq","")
    ("min-tiepoints-per-image","")
    ("number-of-cameras","");

  // Allowed options (includes generic and ba)
  po::options_description allowed_options("Allowed Options");
  allowed_options.add(generic_options).add(ba_options);

  // Commmand line options
  po::options_description cmdline_options;
  cmdline_options.add(generic_options).add(ba_options).add(hidden_options);

  // Config file options
  po::options_description config_file_options;
  config_file_options.add(ba_options).add(hidden_options).add(ignored_options);

  // Positional setup for hidden options
  po::positional_options_description p;
  p.add("input-files", -1);

  // Parse options on command line first
  po::variables_map vm;
  po::store( po::command_line_parser( argc, argv ).options(cmdline_options).positional(p).run(), vm );

  // Print usage message if requested
  std::ostringstream usage;
  usage << "Usage: " << argv[0] << " [options] <camera model files>" << endl
    << endl << allowed_options << endl;
  if ( vm.count("help") ) {
    cout << usage.str() << endl;
    exit(1);
  }

  // Check config file exists
  fs::path cfg(vm["config-file"].as<std::string>());
  if (!fs::exists(cfg) || fs::is_directory(cfg)) {
    std::cerr << "Error: Config file " << cfg
        << " does not exist or is not a regular file." << endl;
    exit(1);
  }

  // Parse options in config file
  std::ifstream config_file_istr( cfg.string().c_str(), std::ifstream::in);
  po::store(po::parse_config_file(config_file_istr, config_file_options), vm);
  po::notify( vm );

  opts.use_user_lambda = (vm.count("lambda") > 0) ? true : false;
  opts.data_dir = data_dir_tmp;

  opts.bundle_adjustment_type = string_to_ba_type(ba_type);

  // Print config options if requested
  if (vm.count("print-config")) {
    cout << opts << endl;
    exit(0);
  }

  // Check we have a control network file
  if ( vm.count("cnet") < 1) {
    cout << "Error: Must specify a control network file!" << endl << endl;
    cout << usage.str() << endl;
    exit(1);
  }

  // Check we have at least one camera model file
  if (opts.camera_files.size() < 1) {
    cout << "Error: Must specify at least one camera model file!" << endl << endl;
    cout << usage.str() << endl;
    exit(1);
  }

  if (opts.control != 0 && opts.control != 1) {
    cout << "Error: Control must be 0 or 1" << endl << endl;
    cout << usage.str() << endl;
    exit(1);
  }

  if (vm.count("results-dir") < 1)
    opts.results_dir = opts.data_dir;

  // If the user provided a sd cutoff for outliers, set the remove_outliers boolean
  // even if it wasn't provided explicitly
  if (!vm["outlier-sd-cutoff"].defaulted())
    opts.remove_outliers = true;

  vw::vw_log().console_log().rule_set().clear();
  vw::vw_log().console_log().rule_set().add_rule(vw::WarningMessage, "console");
  if (vm.count("verbose"))
    vw::vw_log().console_log().rule_set().add_rule(DebugMessage, "console");
  if (vm.count("debug"))
    vw::vw_log().console_log().rule_set().add_rule(VerboseDebugMessage, "console");

  return opts;
}
/* }}} parse_options */

/* {{{ create_data_dir */
void create_data_dir(fs::path dir) {
  // If data directory does not exist, create it
  if (fs::exists(dir) && !fs::is_directory(dir)) {
    std::cerr << "Error: " << dir << " is not a directory." << endl;
    exit(1);
  }
  else
    fs::create_directory(dir);
}
/* }}} create_data_dir */

/* {{{ load_control_network */
boost::shared_ptr<ControlNetwork> load_control_network(fs::path const &file) {
  boost::shared_ptr<ControlNetwork> cnet( new ControlNetwork("Control network"));

  vw_out(DebugMessage) << "Loading control network from file: " << file << endl;

  // Deciding which Control Network we have
  if ( fs::extension(file) == ".net" ) {
    // An ISIS style control network
    vw_out(VerboseDebugMessage) << "\tReading ISIS control network file" << endl;
    cnet->read_isis( file.string() );
  } else if ( fs::extension(file) == ".cnet" ) {
    // A VW binary style
    vw_out(VerboseDebugMessage) << "\tReading VisionWorkbench binary control network file"
      << endl;
    cnet->read_binary( file.string() );
  } else {
    vw_throw( IOErr() << "Unknown control network file extension, \""
      << fs::extension(file) << "\"." );
  }
  return cnet;
}
/* }}} load_control_network */

/* {{{ load_camera_models */
CameraVector load_camera_models(std::vector<fs::path> const &camera_files,
    fs::path const &dir)
{
  vw_out(DebugMessage) << "Loading camera models" << endl;
  CameraVector camera_models;
  std::vector<fs::path>::const_iterator iter;
  for (iter = camera_files.begin(); iter != camera_files.end(); ++iter) {
    fs::path file = *iter;
    // If no parent path is provided for camera files, assume we read them from
    // the data directory
#if (BOOST_VERSION >= 103600)
    if (!file.has_parent_path())
#else
    if (!file.has_branch_path())
#endif
    { file = dir / file; }

    vw_out(VerboseDebugMessage) << "\t" << file << endl;
    boost::shared_ptr<PinholeModel> cam(new PinholeModel());
    cam->read_file(file.string());
    camera_models.push_back(cam);
  }

  return camera_models;
}
/* }}} load_camera_models */

/* {{{ clear_report_files */
void clear_report_files(fs::path cam_file, fs::path point_file, fs::path dir) {
    fs::ofstream c(dir / cam_file, std::ios_base::out);
    c << ""; c.close();
    fs::ofstream p(dir / point_file, std::ios_base::out);
    p << ""; p.close();
}
/* }}} */

/* {{{ write_adjustments */
/* Copied from BundleAdjustUtils.h to remove depedency on Stereo Pipeline modules */
void write_adjustments(std::string const& filename, Vector3 const& position_correction, Quaternion<double> const& pose_correction) {
  std::ofstream ostr(filename.c_str());
  ostr << position_correction[0] << " " << position_correction[1] << " " << position_correction[2] << "\n";
  ostr << pose_correction.w() << " " << pose_correction.x() << " " << pose_correction.y() << " " << pose_correction.z() << " " << "\n";
}
/* }}} write_adjustments */

/* {{{ BundleAdjustmentModel */
// Bundle adjustment functor
class BundleAdjustmentModel : public ba::ModelBase<BundleAdjustmentModel, 6, 3> {

/* {{{ private members */
  typedef Vector<double,6> camera_vector_t;
  typedef Vector<double,3> point_vector_t;

  CameraVector m_cameras;
  boost::shared_ptr<ControlNetwork> m_network;

  // TODO: Should BA model track ground truth points as well? Need to track
  // which points get removed by the outlier-removal process so that the final
  // MSE calculation will use the correct points
  // Alternately, the outlier removal could leave the world points in the
  // control network even when there are < 2 measures associated with them.
  // Then writing out the final world points would skip points with < 2
  // measures (and the constructor for the new model would have to do the same
  // -- that might be a pain).
  std::vector<camera_vector_t> a; // camera parameter adjustments
  std::vector<point_vector_t>  b; // point coordinates
  std::vector<camera_vector_t> a_target;
  std::vector<point_vector_t>  b_target;
  int m_num_pixel_observations;

  double m_camera_position_sigma;
  double m_camera_pose_sigma;
  double m_gcp_sigma;

/* }}} */

public:

/* {{{ constructor */
  BundleAdjustmentModel(CameraVector const& cameras,
                        boost::shared_ptr<ControlNetwork> network,
                        double const camera_position_sigma,
                        double const camera_pose_sigma,
                        double const gcp_sigma) :
    m_cameras(cameras),
    m_network(network),
    a(cameras.size()),
    b(network->size()),
    a_target(cameras.size()),
    b_target(network->size()),
    m_camera_position_sigma(camera_position_sigma),
    m_camera_pose_sigma(camera_pose_sigma),
    m_gcp_sigma(gcp_sigma)
  {
    // Perform a sanity check: make sure no control point has an image_id()
    // greater than the number of cameras. Should only happen if we aren't
    // reading the right control network.
    int camera_count = cameras.size();
    for (ControlNetwork::const_iterator i = network->begin(), end = network->end(); i != end; ++i) {
      for (ControlPoint::const_iterator j = i->begin(), end2 = i->end(); j != end2; ++j) {
        VW_ASSERT(j->image_id() < camera_count, ArgumentErr()
            << "Invalid control point: has image_id() larger than camera vector");
      }
    }

    // Compute the number of observations from the bundle.
    m_num_pixel_observations = 0;
    for (unsigned i = 0; i < network->size(); ++i)
      m_num_pixel_observations += (*network)[i].size();

    // a and a_target start off with every element all zeros.
    for (unsigned j = 0; j < m_cameras.size(); ++j) {
      a_target[j] = camera_vector_t();
      a[j]         = camera_vector_t();
    }

    // b and b_target start off with the initial positions of the 3d points
    for (unsigned i = 0; i < network->size(); ++i) {
      b_target[i] = point_vector_t((*m_network)[i].position());
      b[i]         = point_vector_t((*m_network)[i].position());
    }
  }
/* }}} */

/* {{{ camera, point and pixel accessors */
  // Return a reference to the camera and point parameters.
  camera_vector_t A_parameters(int j) const { return a[j]; }
  camera_vector_t A_target(int j)    const { return a_target[j]; }
  void set_A_parameters(int j, camera_vector_t const& a_j) { a[j] = a_j; }

  point_vector_t B_parameters(int i) const { return b[i]; }
  point_vector_t B_target(int i)    const { return b_target[i]; }
  void set_B_parameters(int i, point_vector_t const& b_i) { b[i] = b_i; }

  CameraVector cameras() { return m_cameras; }
  unsigned num_cameras() const { return a.size(); }
  unsigned num_points()  const { return b.size(); }
  unsigned num_pixel_observations() const { return m_num_pixel_observations; }
/* }}} */

/* {{{ control network accessors */
  // Give access to the control network
  boost::shared_ptr<ControlNetwork> control_network(void) {
    return m_network;
  }

  void control_network(boost::shared_ptr<ControlNetwork> cnet) {
    m_network = cnet;
  }
/* }}} */

/* {{{ A and B inverse covariance */
  // Return the covariance of the camera parameters for camera j.
  inline Matrix<double,camera_params_n,camera_params_n>
  A_inverse_covariance ( unsigned /*j*/ ) const
  {
    Matrix<double,camera_params_n,camera_params_n> result;
    result(0,0) = 1/pow(m_camera_position_sigma,2);
    result(1,1) = 1/pow(m_camera_position_sigma,2);
    result(2,2) = 1/pow(m_camera_position_sigma,2);
    result(3,3) = 1/pow(m_camera_pose_sigma,2);
    result(4,4) = 1/pow(m_camera_pose_sigma,2);
    result(5,5) = 1/pow(m_camera_pose_sigma,2);
    return result;
  }

  // Return the covariance of the point parameters for point i.
  // NB: only applied to Ground Control Points
  inline Matrix<double,point_params_n,point_params_n>
  B_inverse_covariance ( unsigned /*i*/ ) const
  {
    Matrix<double,point_params_n,point_params_n> result;
    result(0,0) = 1/pow(m_gcp_sigma,2);
    result(1,1) = 1/pow(m_gcp_sigma,2);
    result(2,2) = 1/pow(m_gcp_sigma,2);
    return result;
  }
/* }}} */

/* {{{ operator() overload */

  // Given the 'a' vector (camera model parameters) for the j'th
  // image, and the 'b' vector (3D point location) for the i'th
  // point, return the location of b_i on imager j in pixel
  // coordinates.
  Vector2 operator() ( unsigned /*i*/, unsigned j, camera_vector_t const& a_j, point_vector_t const& b_i ) const {
    Vector3 position_correction = subvector(a_j,0,3);
    Vector3 p = subvector(a_j,3,3);
    Quaternion<double> pose_correction = vw::math::euler_to_quaternion(p[0], p[1], p[2], "xyz");

    boost::shared_ptr<CameraModel> cam(
        new AdjustedCameraModel(m_cameras[j], position_correction, pose_correction));
    return cam->point_to_pixel(b_i);
  }
/* }}} */

/* {{{ write_adjustment */
  void write_adjustment(int j, std::string const& filename) const {
    Vector3 position_correction = subvector(a[j],0,3);
    Vector3 p = subvector(a[j],3,3);
    Quaternion<double> pose_correction = vw::math::euler_to_quaternion(p[0], p[1], p[2], "xyz");
    write_adjustments(filename, position_correction, pose_correction);
  }
/* }}} */

/* {{{ adjusted_cameras */
  CameraVector adjusted_cameras() const {
    CameraVector result(m_cameras.size());
    for (unsigned j = 0; j < result.size(); ++j) {
      Vector3 position_correction = subvector(a[j],0,3);
      Vector3 p = subvector(a[j],3,3);
      Quaternion<double> pose_correction = vw::math::euler_to_quaternion(p[0], p[1], p[2], "xyz");
      result[j] = boost::shared_ptr<camera::CameraModel>(
          new AdjustedCameraModel( m_cameras[j], position_correction, pose_correction ) );
    }
    return result;
  }
/* }}} */

/* {{{ error calculations */
  // Errors on the image plane
  void image_errors( std::vector<double>& pix_errors ) {
    pix_errors.clear();
    for (unsigned i = 0; i < m_network->size(); ++i)
      for(unsigned m = 0; m < (*m_network)[i].size(); ++m) {
        int camera_idx = (*m_network)[i][m].image_id();
        Vector2 pixel_error = (*m_network)[i][m].position() - (*this)(i, camera_idx, a[camera_idx],b[i]);
        pix_errors.push_back(norm_2(pixel_error));
      }
  }

  // Errors for camera position
  void camera_position_errors( std::vector<double>& camera_position_errors ) {
    camera_position_errors.clear();
    for (unsigned j=0; j < this->num_cameras(); ++j) {
      Vector3 position_initial = subvector(a_target[j],0,3);
      Vector3 position_now = subvector(a[j],0,3);
      camera_position_errors.push_back(norm_2(position_initial-position_now));
    }
  }

  // Errors for camera pose
  std::string camera_pose_units(){ return "degrees"; }
  void camera_pose_errors( std::vector<double>& camera_pose_errors ) {
    camera_pose_errors.clear();
    for (unsigned j=0; j < this->num_cameras(); ++j) {
      Vector3 pi = subvector(a_target[j],3,3);
      Vector3 pn = subvector(a[j],3,3);
      Quaternion<double> pose_initial = vw::math::euler_to_quaternion(pi[0],pi[1],pi[2],"xyz");
      Quaternion<double> pose_now = vw::math::euler_to_quaternion(pn[0],pn[1],pn[2],"xyz");

      Vector3 axis_initial, axis_now;
      double angle_initial, angle_now;
      pose_initial.axis_angle(axis_initial, angle_initial);
      pose_now.axis_angle(axis_now, angle_now);

      camera_pose_errors.push_back(fabs(angle_initial-angle_now) * 180.0/M_PI);
    }
  }

  // Errors for ground control points
  void gcp_errors( std::vector<double>& gcp_errors ) {
    gcp_errors.clear();
    for (unsigned i=0; i < this->num_points(); ++i) {
      if ((*m_network)[i].type() == ControlPoint::GroundControlPoint)
        gcp_errors.push_back(norm_2(b_target[i] - b[i]));
    }
  }
/* }}} */

/* {{{ write_adjusted_cameras_append */
  void write_adjusted_cameras_append(fs::path const& filename, fs::path const &dir) {
    fs::ofstream ostr(dir / filename,std::ios::app);

    for (unsigned j=0; j < a.size();++j) {
      Vector3 position_correction = subvector(a[j],0,3);
      Vector3 p = subvector(a[j],3,3);
      Quaternion<double> pose_correction = vw::math::euler_to_quaternion(p[0], p[1], p[2],"xyz");

      camera::CAHVORModel cam;
      cam.C = position_correction;
      cam.A = Vector3(1,0,0);
      cam.H = Vector3(0,1,0);
      cam.V = Vector3(0,0,1);
      ostr << j << "\t" << cam.C(0) << "\t" << cam.C(1) << "\t" << cam.C(2) << "\n";
      ostr << j << "\t" << cam.A(0) << "\t" << cam.A(1) << "\t" << cam.A(2) << "\n";
      ostr << j << "\t" << cam.H(0) << "\t" << cam.H(1) << "\t" << cam.H(2) << "\n";
      ostr << j << "\t" << cam.V(0) << "\t" << cam.V(1) << "\t" << cam.V(2) << "\n";
      ostr << j << "\t" << cam.O(0) << "\t" << cam.O(1) << "\t" << cam.O(2) << "\n";
      ostr << j << "\t" << cam.R(0) << "\t" << cam.R(1) << "\t" << cam.R(2) << "\n";
    }
  }
/* }}} write_adjusted_cameras_append */

/* {{{ write_points_append */
  void write_points_append(fs::path const& filename, fs::path const& dir) {
    fs::ofstream ostr(dir / filename, std::ios::app);
    for (unsigned i = 0; i < b.size(); ++i)
      ostr << i << "\t" << b[i][0] << "\t" << b[i][1] << "\t" << b[i][2] << endl;
  }
/* }}} write_points_append */

/* {{{ write_camera_params */
void write_camera_params(fs::path file)
{
  fs::ofstream os(file);
  Vector3 c, p;
  vw_out(DebugMessage) << "Writing camera parameters" << endl;

  CameraVector adj_cameras = this->adjusted_cameras();
  CameraVector::iterator cam;
  for (cam = adj_cameras.begin(); cam != adj_cameras.end(); ++cam) {
    c = (*cam)->camera_center(Vector2());
    p = math::rotation_matrix_to_euler_xyz(
            ((*cam)->camera_pose(Vector2())).rotation_matrix());
    os << std::setprecision(8) << c[0] << "\t" << c[1] << "\t" << c[2] << "\t";
    os << std::setprecision(8) << p[0] << "\t" << p[1] << "\t" << p[2] << endl;
  }
  os.close();
}
/* }}} */

/* {{{ write_world_points */
void write_world_points(fs::path file)
{
  fs::ofstream os(file);

  int num_points = this->num_points();
  vw_out(DebugMessage) << "Writing " << num_points << " world points" << endl;

  for (int i = 0; i < num_points; i++) {
    point_vector_t pos = this->B_parameters(i);
    os << std::setprecision(8) << pos[0] << "\t" << pos[1] << "\t" << pos[2] << endl;
  }
  os.close();
}
/* }}} */

/* {{{ write_adjusted_camera_models */
void write_adjusted_camera_models(ProgramOptions config) {
  for (unsigned int i=0; i < this->num_cameras(); ++i) {
    fs::path file = config.camera_files[i];
    fs::change_extension(file, ".adjust");
    fs::path results_dir = config.results_dir;
    if (config.use_ba_type_dirs)
      results_dir /= ba_type_to_string(config.bundle_adjustment_type);
    file = results_dir / fs::change_extension(config.camera_files[i], ".adjust");
    this->write_adjustment(i, file.string());
  }
}
/* }}} write_adjusted_camera_modesl */

};
/* }}} BundleAdjustmentModel */

/* {{{ remove_outliers */
void remove_outliers(fs::path const &cnet_file,
                     fs::path const &cnet_out_file,
                     fs::path const &data_dir,
                     double const sd_cutoff)
{
  // Check MeanErrorsFile exists (created by bundle adjuster with report_level >= 35)
  if (!fs::exists(MeanErrorsFile) || fs::is_directory(MeanErrorsFile)) {
    vw_throw(vw::IOErr() << "Mean errors file '" << MeanErrorsFile.string() << "' does not exist or is not a regular file");
  }

  // Check cnet_file exists
  if (!fs::exists(cnet_file) || fs::is_directory(cnet_file)) {
    vw_throw(vw::IOErr() << "Control network file '" << cnet_file.string() << "' does not exist or is not a regular file");
  }

  // run cnet_editor on image mean errors file
  std::ostringstream command;
  command << CnetEditor << 
    " -c " << sd_cutoff << 
    " -o " << cnet_out_file << 
    " -d " << data_dir <<
    " " << cnet_file << " " << MeanErrorsFile;
  vw_out(DebugMessage) << "Outlier removal command: " << command.str() << endl;
  int res = system(command.str().c_str());
  if (res == -1) {
    vw_throw(LogicErr() << "system(" << command.str() << ") failed ");
  }
}
/* }}} remove_outliers */

/* {{{ run_bundle_adjustment */
template <class AdjusterT>
void run_bundle_adjustment(AdjusterT &adjuster,
        BundleAdjustReport<AdjusterT> &reporter,
        fs::path &results_dir,
        int max_iter,
        bool save)
{
  double abs_tol=1e10, rel_tol=1e10;
  double overall_delta = 2;
  while (overall_delta) {
    //reporter.loop_tie_in();
    if (adjuster.iterations() >= max_iter || abs_tol < 1e-3 || rel_tol < 1e-3)
      break;

    // Performing iteration
    overall_delta = adjuster.update(abs_tol, rel_tol);

    if (save) {
      //Write this iterations camera and point data
      adjuster.bundle_adjust_model().write_adjusted_cameras_append(CameraParamsReportFile, results_dir);
      adjuster.bundle_adjust_model().write_points_append(PointsReportFile, results_dir);
    }
  }

  // if config.report_level >= 35, this will write the image errors file needed
  // for outlier removal
  reporter.end_tie_in();
}
/* }}} */

/* {{{ adjust_bundles */
template <class AdjusterT, class CostT>
void adjust_bundles(BundleAdjustmentModel &ba_model, CostT const &cost_func,
        ProgramOptions const &config, std::string ba_type_str)
{
  AdjusterT bundle_adjuster(ba_model, cost_func);
  vw_out(DebugMessage) << "Running bundle adjustment" << endl;

  fs::path results_dir = config.results_dir;
  if (config.use_ba_type_dirs) {
    std::string type_dir = ba_type_to_string(config.bundle_adjustment_type);
    if (config.remove_outliers)
      type_dir += "_no_outliers";
    results_dir /= type_dir;
  }

  // Set lambda if user has requested it
  if (config.use_user_lambda)
    bundle_adjuster.set_lambda(config.lambda);

  // Set control as specified
  bundle_adjuster.set_control(config.control);

  // Clear the monitoring text files to be used for saving camera params
  if (config.save_iteration_data)
    clear_report_files(CameraParamsReportFile, PointsReportFile, results_dir);

  // Configure reporter
  BundleAdjustReport<AdjusterT>
      reporter(ba_type_str, ba_model, bundle_adjuster, config.report_level );

  // Run bundle adjustment
  run_bundle_adjustment<AdjusterT>(bundle_adjuster, reporter, results_dir,
      config.max_iterations, config.save_iteration_data);

 // If we want to remove outliers, do the process again
  if (config.remove_outliers) {
    // Remove outliers
    fs::path cnet_file = config.data_dir/config.cnet_file;
    remove_outliers(cnet_file, ProcessedCnetFile, results_dir, config.outlier_sd_cutoff);

    // Load new control network with outliers removed
    boost::shared_ptr<ControlNetwork> cnet = load_control_network(results_dir/ProcessedCnetFile);

    // Make a new bundle adjustment model
    BundleAdjustmentModel ba_model_no_outliers(ba_model.cameras(), cnet,
        config.camera_position_sigma, config.camera_pose_sigma, config.gcp_sigma);

    // Make a new adjuster
    AdjusterT bundle_adjuster_no_outliers(ba_model_no_outliers, cost_func);
    vw_out(DebugMessage) << "Running bundle adjustment with outliers removed" << endl;

    // Set lambda if user has requested it
    if (config.use_user_lambda)
      bundle_adjuster_no_outliers.set_lambda(config.lambda);
    
    bundle_adjuster.set_control(config.control);

    // Configure reporter
    BundleAdjustReport<AdjusterT>
        reporter(ba_type_str + " No Outliers", ba_model_no_outliers,
            bundle_adjuster_no_outliers, config.report_level );

    // Run bundle adjustment again
    run_bundle_adjustment<AdjusterT>(bundle_adjuster_no_outliers, reporter, results_dir,
        config.max_iterations, config.save_iteration_data);

    // Set the input BA model to the new, no-outliers model
    ba_model = ba_model_no_outliers;
  }

  // remove image mean errors file
  //fs::remove(MeanErrorsFile);

}
/* }}} adjust_bundles */

int main(int argc, char* argv[]) {
  ProgramOptions config = parse_options(argc, argv);

  fs::path results_dir = config.results_dir;
  if (config.use_ba_type_dirs)
    results_dir /= ba_type_to_string(config.bundle_adjustment_type);
  create_data_dir(results_dir);

  fs::path cam_file_initial = results_dir / "cam_initial.txt";
  fs::path wp_file_initial  = results_dir / "wp_initial.txt";
  fs::path cam_file_final   = results_dir / "cam_final.txt";
  fs::path wp_file_final    = results_dir / "wp_final.txt";

  fs::path cnet_file        = config.data_dir / config.cnet_file;
  boost::shared_ptr<ControlNetwork> cnet = load_control_network(cnet_file);

  CameraVector camera_models = load_camera_models(config.camera_files, config.data_dir);

  BundleAdjustmentModel ba_model(camera_models, cnet,
      config.camera_position_sigma, config.camera_pose_sigma, config.gcp_sigma);

  // Write initial camera parameters and world points
  ba_model.write_camera_params(cam_file_initial);
  ba_model.write_world_points(wp_file_initial);

  // Run bundle adjustment according to user-specified type
  switch (config.bundle_adjustment_type) {
    case REF:
      adjust_bundles<AdjustRef<BundleAdjustmentModel, L2Error>, L2Error >
        (ba_model, L2Error(), config, "Reference");
      break;
    case SPARSE:
      adjust_bundles<AdjustSparse<BundleAdjustmentModel, L2Error>, L2Error >
        (ba_model, L2Error(), config, "Sparse");
      break;
    case SPARSE_HUBER:
      adjust_bundles<AdjustSparse<BundleAdjustmentModel, HuberError>, HuberError >
        (ba_model, HuberError(config.huber_param), config, "Sparse Huber");
      break;
    case SPARSE_CAUCHY:
      adjust_bundles<AdjustSparse<BundleAdjustmentModel, CauchyError>, CauchyError >
        (ba_model, CauchyError(config.cauchy_param), config, "Sparse Cauchy");
      break;
    case ROBUST_REF:
      adjust_bundles<AdjustRobustRef<BundleAdjustmentModel, L2Error>, L2Error >
        (ba_model, L2Error(), config, "Robust Reference");
      break;
    case ROBUST_SPARSE:
      adjust_bundles<AdjustRobustSparse<BundleAdjustmentModel, L2Error>, L2Error >
        (ba_model, L2Error(), config, "Robust Sparse");
      break;
  }
  
  // Do covariance calculation:
  // a. set lambda = 0
  // b. run bundle adjustment for one iteration
  // c. run adjuster.covCalc()
  //        covCalc will return a vector (length = numCameras)
  //        of 6x6 covariance matrices
  //        (may need to do this inside adjust_bundles, in which
  //         case adjust_bundles will return a vector of covariance matrices)
  // d. write covariance matrices to file
  // 
  // In results calculations:
  // a. for each estimated camera parameter, check whether true parameter is
  //    within 1.96 * sqrt of appropriate element of corresponding camera
  //    covariance matrix
 
  // Write post-adjustment camera model files
  ba_model.write_adjusted_camera_models(config);

  // Write post-adjustment camera parameters and world points
  // cam params should include estimate_sigma (sqrt of corresponding diag
  // element of covariance matrix)
  ba_model.write_camera_params(cam_file_final);
  ba_model.write_world_points(wp_file_final);

  return 0;
}
