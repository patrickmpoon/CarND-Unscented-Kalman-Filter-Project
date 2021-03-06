#include "ukf.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  is_initialized_ = false;

  //set state dimension
  n_x_ = 5;

  //set augmented dimension
  n_aug_ = 7;

  // initial state vector
  x_pred_ = VectorXd(5);

  // initial covariance matrix
  P_pred_ = MatrixXd(n_x_, n_x_);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 3.80;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = 0.3;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  //create matrix with predicted sigma points as columns
  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  //create vector for weights_
  weights_ = VectorXd(2 * n_aug_ + 1);

  //define spreading parameter
  lambda_ = 3 - n_aug_;

  //create sigma point matrix
  Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);

  //create augmented mean vector
  x_aug = VectorXd(7);

  //create augmented state covariance
  P_aug = MatrixXd(7, 7);
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /*****************************************************************************
   *  Initialization
   ****************************************************************************/

  if (!is_initialized_) {
    cout << "UKF: " << endl;

    if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
      /**
      Convert radar from polar to cartesian coordinates and initialize state.
      */
      float rho = meas_package.raw_measurements_[0];
      float phi = meas_package.raw_measurements_[1];
      float px = rho * cos(phi);
      float py = rho * sin(phi);

      x_pred_ << px,
            py,
            0,
            0,
            0;
    }
    else if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
      /**
      Initialize state.
      */
      //set the state with the initial location and zero velocity
      x_pred_ << meas_package.raw_measurements_[0],
            meas_package.raw_measurements_[1],
            0,
            0,
            0;
    }

    previous_timestamp_ = meas_package.timestamp_;

    // done initializing, no need to predict or update
    is_initialized_ = true;

    return;
  }

  double delta_t = (meas_package.timestamp_ - previous_timestamp_) / 1000000.0;
  Prediction(delta_t);

  /*****************************************************************************
  *  Update
  ****************************************************************************/

  if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
    UpdateRadar(meas_package);
  }
  else if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
    UpdateLidar(meas_package);
  }

  previous_timestamp_ = meas_package.timestamp_;
}

/**
 * Creates sigma points
 * @param Xsig_out Reference to state mean
 */
void UKF::GenerateSigmaPoints(MatrixXd* Xsig_out) {
  //create augmented mean state
  x_aug.fill(0.0);
  x_aug.head(5) = x_pred_;
  x_aug(5) = 0;
  x_aug(6) = 0;

  //create augmented covariance matrix
  P_aug.fill(0.0);
  P_aug.topLeftCorner(5,5) = P_pred_;
  P_aug(5,5) = std_a_ * std_a_;
  P_aug(6,6) = std_yawdd_ * std_yawdd_;

  //create square root matrix
  MatrixXd L = P_aug.llt().matrixL();

  //create augmented sigma points
  Xsig_aug.fill(0.0);
  Xsig_aug.col(0)  = x_aug;
  for (int i = 0; i < n_aug_; i++)
  {
    Xsig_aug.col(i + 1) = x_aug + sqrt(lambda_ + n_aug_) * L.col(i);
    Xsig_aug.col(i + 1 + n_aug_) = x_aug - sqrt(lambda_ + n_aug_) * L.col(i);
  }

  *Xsig_out = Xsig_aug;
}

/**
 * Predicts Sigma Points
 * @param Xsig_out Reference to state mean
 * @param n_aug_ Augmented state dimension
 * @param delta_t Time difference since last measurement
 */
void UKF::PredictSigmaPoints(MatrixXd *Xsig_out, int n_aug_, double delta_t) {
  //predict sigma points
  for (int i = 0; i < 1 + (2 * n_aug_); i++)
  {
    //extract values for better readability
    double p_x = Xsig_aug(0,i);
    double p_y = Xsig_aug(1,i);
    double v = Xsig_aug(2,i);
    double yaw = Xsig_aug(3,i);
    double yawd = Xsig_aug(4,i);
    double nu_a = Xsig_aug(5,i);
    double nu_yawdd = Xsig_aug(6,i);

    //predicted state values
    double px_p, py_p;

    //avoid division by zero
    if (fabs(yawd) > 0.001) {
      px_p = p_x + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
      py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
    }
    else {
      px_p = p_x + (v * delta_t * cos(yaw));
      py_p = p_y + (v * delta_t * sin(yaw));
    }

    double v_p = v;
    double yaw_p = yaw + yawd * delta_t;
    double yawd_p = yawd;

    //add noise
    px_p = px_p + (0.5 * nu_a * delta_t * delta_t * cos(yaw));
    py_p = py_p + (0.5 * nu_a * delta_t * delta_t * sin(yaw));
    v_p = v_p + nu_a * delta_t;

    yaw_p  = yaw_p  + (0.5 * nu_yawdd * delta_t * delta_t);
    yawd_p = yawd_p + (nu_yawdd * delta_t);

    //write predicted sigma point into right column
    Xsig_pred_(0,i) = px_p;
    Xsig_pred_(1,i) = py_p;
    Xsig_pred_(2,i) = v_p;
    Xsig_pred_(3,i) = yaw_p;
    Xsig_pred_(4,i) = yawd_p;
  }

  *Xsig_out = Xsig_pred_;
};

/**
 * Predict Mean And Covariance
 * @param x_pred_out Reference to state mean
 * @param P_pred_out Reference to state covariance
 */
void UKF::PredictMeanAndCovariance(VectorXd* x_pred_out, MatrixXd* P_pred_out) {
  // set weights_
  weights_(0) = lambda_ / (lambda_ + n_aug_);
  for (int i = 1; i < 2 * n_aug_ + 1; i++) {
    weights_(i) = 0.5 / (n_aug_ + lambda_);
  }

  //predicted state mean
  VectorXd x_pred_ = VectorXd(5);
  x_pred_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    x_pred_ = x_pred_ + weights_(i) * Xsig_pred_.col(i);
  }

  //predicted state covariance matrix
  MatrixXd P_pred_ = MatrixXd(n_x_, n_x_);
  P_pred_.fill(0.0);

  for (int i = 0; i < 2 * n_aug_ + 1; i++) {

    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_pred_;

    //angle normalization
    while (x_diff(3) > M_PI) x_diff(3) -= 2. * M_PI;
    while (x_diff(3) < -M_PI) x_diff(3) += 2. * M_PI;

    P_pred_ = P_pred_ + weights_(i) * x_diff * x_diff.transpose() ;
  }

  *x_pred_out = x_pred_;
  *P_pred_out = P_pred_;
};

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {
  GenerateSigmaPoints(&Xsig_aug);
  PredictSigmaPoints(&Xsig_pred_, n_aug_, delta_t);
  PredictMeanAndCovariance(&x_pred_, &P_pred_);
}

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {
  // Laser updates
  //measurement matrix
  MatrixXd H_ = MatrixXd(2, 5);
  H_ << 1, 0, 0, 0, 0,
        0, 1, 0, 0, 0;
  MatrixXd R_ = MatrixXd(2, 2);

  //measurement covariance matrix - laser
  R_ << 0.0225, 0,
        0, 0.0225;

  VectorXd z = meas_package.raw_measurements_;
  VectorXd z_pred = H_ * x_pred_;
  VectorXd y = z - z_pred;
  MatrixXd Ht = H_.transpose();
  MatrixXd S = H_ * P_pred_ * Ht + R_;
  MatrixXd Si = S.inverse();
  MatrixXd PHt = P_pred_ * Ht;
  MatrixXd K = PHt * Si;

  //new estimate
  x_pred_ = x_pred_ + (K * y);
  long x_size = x_pred_.size();
  MatrixXd I = MatrixXd::Identity(x_size, x_size);
  P_pred_ = (I - K * H_) * P_pred_;
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {
  /*******************************************************************************
   * PREDICT RADAR SIGMA POINTS
   ******************************************************************************/

  //set measurement dimension, radar can measure r, phi, and r_dot
  int n_z_ = 3;

  //create matrix for sigma points in measurement space
  MatrixXd Zsig = MatrixXd(n_z_, 2 * n_aug_ + 1);

  //transform sigma points into measurement space
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {

    // extract values for better readibility
    double p_x = Xsig_pred_(0,i);
    double p_y = Xsig_pred_(1,i);
    double v  = Xsig_pred_(2,i);
    double yaw = Xsig_pred_(3,i);

    double v1 = cos(yaw)*v;
    double v2 = sin(yaw)*v;

    // measurement model
    // rho
    Zsig(0,i) = sqrt(p_x * p_x + p_y * p_y);
    // phi
    Zsig(1,i) = atan2(p_y,p_x);
    // rho_dot
    Zsig(2,i) = (p_x * v1 + p_y * v2 ) / sqrt(p_x * p_x + p_y * p_y);
  }

  //mean predicted measurement
  VectorXd z_pred = VectorXd(n_z_);
  z_pred.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }

  //measurement covariance matrix S
  MatrixXd S = MatrixXd(n_z_,n_z_);
  S.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;

    //angle normalization
    while (z_diff(1) >  M_PI) z_diff(1) -= 2. * M_PI;
    while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

    S = S + weights_(i) * z_diff * z_diff.transpose();
  }

  //add measurement noise covariance matrix
  MatrixXd R = MatrixXd(n_z_,n_z_);
  R << std_radr_ * std_radr_, 0, 0,
       0, std_radphi_ * std_radphi_, 0,
       0, 0, std_radrd_ * std_radrd_;
  S = S + R;


  /*******************************************************************************
   * UPDATE RADAR
   ******************************************************************************/

  //create example vector for incoming radar measurement
  VectorXd z = meas_package.raw_measurements_;

  //create matrix for cross correlation Tc
  MatrixXd Tc = MatrixXd(n_x_, n_z_);

  //calculate cross correlation matrix
  Tc.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {

    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;

    //angle normalization
    while (z_diff(1) >  M_PI) z_diff(1) -= 2. * M_PI;
    while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_pred_;

    //angle normalization
    while (x_diff(3) >  M_PI) x_diff(3) -= 2. * M_PI;
    while (x_diff(3) <- M_PI) x_diff(3) += 2. * M_PI;

    Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
  }

  //Kalman gain K;
  MatrixXd K = Tc * S.inverse();

  //residual
  VectorXd z_diff = z - z_pred;

  //angle normalization
  while (z_diff(1) >  M_PI) z_diff(1) -= 2. * M_PI;
  while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

  //update state mean and covariance matrix
  x_pred_ = x_pred_ + K * z_diff;
  P_pred_ = P_pred_ - K * S * K.transpose();
}
