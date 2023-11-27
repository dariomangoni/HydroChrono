/*********************************************************************
 * @file wave_types.cpp
 *
 * @brief implementation file for Wavebase and classes inheriting from WaveBase.
 *********************************************************************/
#include <hydroc/helper.h>
#include <hydroc/wave_types.h>
#include <unsupported/Eigen/Splines>

Eigen::VectorXd NoWave::GetForceAtTime(double t) {
    unsigned int dof = num_bodies_ * 6;
    Eigen::VectorXd f(dof);
    for (int i = 0; i < dof; i++) {
        f[i] = 0.0;
    }
    return f;
}

RegularWave::RegularWave() {
    num_bodies_ = 1;
}

RegularWave::RegularWave(unsigned int num_b) {
    num_bodies_ = num_b;
}

void RegularWave::Initialize() {
    // set up regular waves here, call other helper functions as necessary
    int total_dofs = 6 * num_bodies_;
    excitation_force_mag_.resize(total_dofs);
    excitation_force_phase_.resize(total_dofs);
    force_.resize(total_dofs);

    double wave_omega_delta = GetOmegaDelta();
    double freq_index_des   = (regular_wave_omega_ / wave_omega_delta) - 1;
    for (int b = 0; b < num_bodies_; b++) {
        for (int rowEx = 0; rowEx < 6; rowEx++) {
            int body_offset = 6 * b;
            // why are these always 0? vvv TODO check/change this
            excitation_force_mag_[body_offset + rowEx]   = GetExcitationMagInterp(b, rowEx, 0, freq_index_des);
            excitation_force_phase_[body_offset + rowEx] = GetExcitationPhaseInterp(b, rowEx, 0, freq_index_des);
        }
    }
}

void RegularWave::AddH5Data(std::vector<HydroData::RegularWaveInfo>& reg_h5_data) {
    wave_info_ = reg_h5_data;
}

Eigen::VectorXd RegularWave::GetForceAtTime(double t) {
    unsigned int dof = num_bodies_ * 6;
    Eigen::VectorXd f(dof);
    // initialize the force here:
    for (int b = 0; b < num_bodies_; b++) {
        int body_offset = 6 * b;
        for (int rowEx = 0; rowEx < 6; rowEx++) {
            f[body_offset + rowEx] = excitation_force_mag_[body_offset + rowEx] * regular_wave_amplitude_ *
                                     cos(regular_wave_omega_ * t + excitation_force_phase_[rowEx]);
        }
    }
    return f;
}

double RegularWave::GetOmegaDelta() const {
    double omega_max = wave_info_[0].freq_list[wave_info_[0].freq_list.size() - 1];
    double num_freqs = wave_info_[0].freq_list.size();
    return omega_max / num_freqs;
}

double RegularWave::GetExcitationMagInterp(int b, int i, int j, double freq_index_des) const {
    double freq_interp_val    = freq_index_des - floor(freq_index_des);
    double excitationMagFloor = wave_info_[b].excitation_mag_matrix(i, j, (int)floor(freq_index_des));
    double excitationMagCeil  = wave_info_[b].excitation_mag_matrix(i, j, (int)floor(freq_index_des) + 1);
    double excitationMag      = (freq_interp_val * (excitationMagCeil - excitationMagFloor)) + excitationMagFloor;

    return excitationMag;
}

double RegularWave::GetExcitationPhaseInterp(int b, int i, int j, double freq_index_des) const {
    double freq_interp_val      = freq_index_des - floor(freq_index_des);  // look into c++ modf TODO
    double excitationPhaseFloor = wave_info_[b].excitation_phase_matrix(
        i, j, (int)floor(freq_index_des));  // TODO check if freq_index_des is >0, if so just cast instead of floor
    double excitationPhaseCeil = wave_info_[b].excitation_phase_matrix(i, j, (int)floor(freq_index_des) + 1);
    double excitationPhase = (freq_interp_val * (excitationPhaseCeil - excitationPhaseFloor)) + excitationPhaseFloor;

    return excitationPhase;
}

std::vector<double> ComputeWaveNumbers(const std::vector<double>& omegas,
                                       double water_depth,
                                       double g           = 9.81,
                                       double tolerance   = 1e-6,
                                       int max_iterations = 100) {
    std::vector<double> wave_numbers(omegas.size());

    for (size_t i = 0; i < omegas.size(); ++i) {
        double omega = omegas[i];

        // Initial guess for wave number (using deep water approximation)
        double k = omega * omega / g;

        int iterations = 0;
        double error   = 1.0;
        while (error > tolerance && iterations < max_iterations) {
            double tanh_kh = std::tanh(k * water_depth);
            double f       = omega * omega - g * k * tanh_kh;
            double df      = -2.0 * g * tanh_kh - g * k * water_depth * (1.0 - tanh_kh * tanh_kh);

            double delta_k = f / df;
            k -= delta_k;
            error = std::abs(delta_k);
            iterations++;
        }

        wave_numbers[i] = k;
    }

    return wave_numbers;
}

std::vector<double> FreeSurfaceElevation(const Eigen::VectorXd& freqs_hz,
                                         const Eigen::VectorXd& spectral_densities,
                                         const Eigen::VectorXd& time_index,
                                         double water_depth,
                                         int seed) {
    double delta_f = freqs_hz(Eigen::last) / freqs_hz.size();
    std::vector<double> omegas(freqs_hz.size());

    for (size_t i = 0; i < freqs_hz.size(); ++i) {
        omegas[i] = 2 * M_PI * freqs_hz[i];
    }

    std::vector<double> wave_numbers = ComputeWaveNumbers(omegas, water_depth);

    std::vector<double> A(spectral_densities.size());
    for (size_t i = 0; i < spectral_densities.size(); ++i) {
        A[i] = 2 * spectral_densities[i] * delta_f;
    }

    std::vector<double> sqrt_A(A.size());
    for (size_t i = 0; i < A.size(); ++i) {
        sqrt_A[i] = std::sqrt(A[i]);
    }

    std::vector<std::vector<double>> omegas_t(time_index.size(), std::vector<double>(omegas.size()));
    for (size_t i = 0; i < time_index.size(); ++i) {
        for (size_t j = 0; j < omegas.size(); ++j) {
            omegas_t[i][j] = time_index[i] * omegas[j];
        }
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 2 * M_PI);
    std::vector<double> phases(omegas.size());
    for (size_t i = 0; i < phases.size(); ++i) {
        phases[i] = dist(rng);
    }

    std::vector<double> eta(time_index.size(), 0.0);
    for (size_t i = 0; i < spectral_densities.size(); ++i) {
        for (size_t j = 0; j < time_index.size(); ++j) {
            eta[j] += sqrt_A[i] * std::cos(omegas_t[j][i] + phases[i]);
        }
    }

    return eta;
}

std::vector<std::array<double, 3>> CreateFreeSurface3DPts(const std::vector<double>& eta,
                                                          const Eigen::VectorXd& t_vec) {
    std::vector<std::array<double, 3>> surface(t_vec.size() * 2);

    for (size_t i = 0; i < t_vec.size(); ++i) {
        double t = -1 * t_vec[i];
        double z = eta[i];

        surface[2 * i]     = {t, -10.0, z};
        surface[2 * i + 1] = {t, 10.0, z};
    }

    return surface;
}

std::vector<std::array<size_t, 3>> CreateFreeSurfaceTriangles(size_t eta_size) {
    std::vector<std::array<size_t, 3>> triangles;

    for (size_t i = 0; i < eta_size / 2 - 1; ++i) {
        triangles.push_back({2 * i, 2 * i + 1, 2 * i + 3});
        triangles.push_back({2 * i, 2 * i + 3, 2 * i + 2});
    }

    return triangles;
}
#include <iomanip>

void WriteFreeSurfaceMeshObj(const std::vector<std::array<double, 3>>& points,
                             const std::vector<std::array<size_t, 3>>& triangles,
                             const std::string& file_name) {
    std::ofstream out(file_name);
    if (!out) {
        std::cerr << "Failed to open " << file_name << std::endl;
        return;
    }

    // Write header
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);
    out << "# Wavefront OBJ file exported by HydroChrono" << std::endl;
    out << "# File Created: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << std::endl << std::endl;

    // Write vertices
    out << "# Vertices: " << points.size() << std::endl << std::endl;
    out << std::fixed << std::setprecision(6);
    for (const auto& point : points) {
        out << "v ";
        out << std::setw(14) << point[0] << ' ';
        out << std::setw(14) << point[1] << ' ';
        out << std::setw(14) << point[2] << std::endl;
    }
    out << std::endl;

    // Write faces
    out << "# Faces: " << triangles.size() << std::endl << std::endl;
    for (const auto& triangle : triangles) {
        out << "f ";
        out << std::setw(9) << triangle[0] + 1;
        out << std::setw(9) << triangle[1] + 1;
        out << std::setw(9) << triangle[2] + 1 << std::endl;
    }

    out.close();
}

IrregularWaves::IrregularWaves(const IrregularWaveParams& params)
    : num_bodies_(params.num_bodies_),
      eta_file_path_(params.eta_file_path_),
      wave_height_(params.wave_height_),
      wave_period_(params.wave_period_),
      peak_enhancement_factor_(params.peak_enhancement_factor_),
      simulation_dt_(params.simulation_dt_),
      simulation_duration_(params.simulation_duration_),
      ramp_duration_(params.ramp_duration_),
      seed_(params.seed_) {}

void IrregularWaves::InitializeIRFVectors() {
    ex_irf_sampled_.resize(num_bodies_);
    ex_irf_time_sampled_.resize(num_bodies_);
    ex_irf_width_sampled_.resize(num_bodies_);

    std::vector<Eigen::MatrixXd> ex_irf_old(num_bodies_);
    std::vector<Eigen::VectorXd> ex_irf_time_old(num_bodies_);

    for (unsigned int b = 0; b < num_bodies_; b++) {
        ex_irf_sampled_[b]      = GetExcitationIRF(b);
        ex_irf_time_sampled_[b] = wave_info_[b].excitation_irf_time;
        CalculateWidthIRF();
    }

    // Resample excitation IRF time series
    if (simulation_dt_ > 0.0) {
        ResampleIRF(simulation_dt_);
    }

    if (!eta_file_path_.empty()) {
        ReadEtaFromFile();
        spectrumCreated_ = false;
    } else if (wave_height_ != 0.0 && wave_period_ != 0.0) {
        CreateSpectrum();
        CreateFreeSurfaceElevation();
        spectrumCreated_ = true;
    }
}

std::vector<double> IrregularWaves::GetSpectrum() {
    if (!spectrumCreated_) {
        throw std::runtime_error(
            "Spectrum has not been created. Initialize with wave height and period to create spectrum.");
    }
    return spectrum_;
}

std::vector<double> IrregularWaves::GetFreeSurfaceElevation() {
    return free_surface_elevation_sampled_;
}

std::vector<double> IrregularWaves::GetEtaTimeData() {
    return time_data_;
}

void IrregularWaves::ReadEtaFromFile() {
    std::cout << "Reading eta file " << eta_file_path_ << "." << std::endl;
    std::ifstream file(eta_file_path_);
    if (!file) {
        throw std::runtime_error("Unable to open file at: " + eta_file_path_ + ".");
    }

    std::string line;
    double time, eta;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        char delimiter;
        if (!(ss >> time >> delimiter >> eta) || delimiter != ':') {
            throw std::runtime_error("Could not parse line: " + line + ".");
        }
        time_data_.push_back(time);
        free_surface_elevation_sampled_.push_back(eta);
    }
    std::cout << "Finished reading eta file." << std::endl;
}

Eigen::MatrixXd IrregularWaves::GetExcitationIRF(int b) const {
    return wave_info_[b].excitation_irf_matrix;
}

void IrregularWaves::AddH5Data(std::vector<HydroData::IrregularWaveInfo>& irreg_h5_data,
                               HydroData::SimulationParameters& sim_data) {
    wave_info_ = irreg_h5_data;

    InitializeIRFVectors();
}

Eigen::VectorXd IrregularWaves::GetForceAtTime(double t) {
    unsigned int total_dofs = num_bodies_ * 6;
    Eigen::VectorXd f(total_dofs);
    for (int i = 0; i < total_dofs; i++) {
        f[i] = 0.0;
    }

    for (int body = 0; body < num_bodies_; body++) {
        // Loop through the DOFs
        for (int dof = 0; dof < 6; ++dof) {
            // Compute the convolution for the current DOF
            double f_dof          = ExcitationConvolution(body, dof, t);
            unsigned int b_offset = body * 6;
            f[b_offset + dof]     = f_dof;
        }
    }

    return f;
}

void IrregularWaves::ResampleIRF(double dt) {
    for (unsigned int b = 0; b < num_bodies_; b++) {
        auto& time_array  = ex_irf_time_sampled_[b];
        auto& width_array = ex_irf_width_sampled_[b];
        auto& val_array   = ex_irf_sampled_[b];

        // Copy time array
        auto time_array_old = time_array;

        // 1) Resample time
        auto t0    = time_array_old[0];
        auto t1    = time_array_old[time_array_old.size() - 1];
        time_array = Eigen::VectorXd::LinSpaced(static_cast<int>(ceil((t1 - t0) / dt)), t0, t1);

        // 2) Resample width
        CalculateWidthIRF();

        // 3) Resample values
        assert(val_array.rows() == 6);
        Eigen::MatrixXd vals_new(6, time_array.size());

        // We need to scale t to be [0,1] for spline use
        Eigen::VectorXd t_old_scaled = Eigen::VectorXd::LinSpaced(time_array_old.size(), 0, 1);
        Eigen::VectorXd t_new_scaled = Eigen::VectorXd::LinSpaced(time_array.size(), 0, 1);

        // Use spline to get new values
        Eigen::Spline<double, 6> spline =
            Eigen::SplineFitting<Eigen::Spline<double, 6>>::Interpolate(val_array, 3, t_old_scaled);
        for (int i = 0; i < time_array.rows(); i++) {
            vals_new.col(i) = spline(t_new_scaled[i]);
        }

        val_array = vals_new;
    }
}

void IrregularWaves::CalculateWidthIRF() {
    for (unsigned int b = 0; b < num_bodies_; b++) {
        auto& time_array  = ex_irf_time_sampled_[b];
        auto& width_array = ex_irf_width_sampled_[b];

        width_array.resize(time_array.size());
        for (int ii = 0; ii < width_array.size(); ii++) {
            width_array[ii] = 0.0;
            if (ii < time_array.size() - 1) {
                width_array[ii] += 0.5 * abs(time_array[ii + 1] - time_array[ii]);
            }
            if (ii > 0) {
                width_array[ii] += 0.5 * abs(time_array[ii] - time_array[ii - 1]);
            }
        }
    }
}

Eigen::VectorXd IrregularWaves::SetSpectrumFrequencies(double start, double end, int num_points) {
    Eigen::VectorXd result(num_points);
    double step = (end - start) / (num_points - 1);

    for (int i = 0; i < num_points; ++i) {
        result[i] = start + i * step;
    }

    spectrum_frequencies_ = result;

    return result;
}

void IrregularWaves::CreateSpectrum() {
    // Define the frequency vector
    spectrum_frequencies_ = Eigen::VectorXd::LinSpaced(1000, 0.001, 1.0);

    // Calculate the Pierson-Moskowitz Spectrum
    spectral_densities_ =
        JONSWAPSpectrumHz(spectrum_frequencies_, wave_height_, wave_period_, peak_enhancement_factor_);

    // Open a file stream for writing
    std::ofstream outputFile("spectral_densities.txt");

    // Check if the file stream is open
    if (outputFile.is_open()) {
        // Write the spectral densities and their corresponding frequencies to the file
        for (size_t i = 0; i < spectral_densities_.size(); ++i) {
            outputFile << spectrum_frequencies_[i] << " : " << spectral_densities_[i] << std::endl;
        }

        // Close the file stream
        outputFile.close();
    } else {
        std::cerr << "Unable to open file for writing." << std::endl;
    }
}

// TODO put spectrum functions in a new namespace (when we have more options?)
Eigen::VectorXd PiersonMoskowitzSpectrumHz(Eigen::VectorXd& f, double Hs, double Tp) {
    // Sort the frequency vector
    std::sort(f.begin(), f.end());

    // Initialize the spectral densities vector
    Eigen::VectorXd spectral_densities(f.size());

    // Calculate the spectral densities
    for (size_t i = 0; i < f.size(); ++i) {
        spectral_densities[i] = 1.25 * std::pow(1 / Tp, 4) * std::pow(Hs / 2, 2) * std::pow(f[i], -5) *
                                std::exp(-1.25 * std::pow(1 / Tp, 4) * std::pow(f[i], -4));
    }

    return spectral_densities;
}

Eigen::VectorXd JONSWAPSpectrumHz(Eigen::VectorXd& f, double Hs, double Tp, double gamma) {
    auto spectral_densities = PiersonMoskowitzSpectrumHz(f, Hs, Tp);

    // Scale spectral densities from PM to JONSWAP with gamma factor
    for (size_t i = 0; i < spectral_densities.size(); ++i) {
        double sigma;
        if (f[i] <= 1.0 / Tp) {
            sigma = 0.07;
        } else {
            sigma = 0.09;
        }
        spectral_densities[i] *= pow(gamma, exp(-(1.0 / (2.0 * pow(sigma, 2))) * pow(f[i] * Tp - 1.0, 2)));
    }

    return spectral_densities;
}

void IrregularWaves::CreateFreeSurfaceElevation() {
    // Create a time index vector
    // UpdateNumTimesteps();
    int num_timesteps = static_cast<int>(simulation_duration_ / simulation_dt_) + 1;

    double t_irf_min = 0.0;
    double t_irf_max = 0.0;
    for (auto ii = 0; ii < ex_irf_time_sampled_.size(); ii++) {
        if (ex_irf_time_sampled_[ii][0] < t_irf_min) {
            t_irf_min = ex_irf_time_sampled_[ii][0];
        }
        if (ex_irf_time_sampled_[ii][0] > t_irf_max) {
            t_irf_max = ex_irf_time_sampled_[ii][0];
        }
        if (ex_irf_time_sampled_[ii][ex_irf_time_sampled_[ii].size() - 1] > t_irf_max) {
            t_irf_max = ex_irf_time_sampled_[ii][ex_irf_time_sampled_[ii].size() - 1];
        }
        if (ex_irf_time_sampled_[ii][ex_irf_time_sampled_[ii].size() - 1] < t_irf_min) {
            t_irf_min = ex_irf_time_sampled_[ii][ex_irf_time_sampled_[ii].size() - 1];
        }
    }

    auto time_array = Eigen::VectorXd::LinSpaced(num_timesteps, 0, simulation_duration_ + 2 * (t_irf_max - t_irf_min));

    // Save time array as a std::vector
    free_surface_time_sampled_.resize(time_array.size());
    Eigen::VectorXd::Map(&free_surface_time_sampled_[0], time_array.size()) = time_array;
    for (int ii = 0; ii < free_surface_time_sampled_.size(); ii++) {
        free_surface_time_sampled_[ii] += -t_irf_max;
    }

    std::cout << "Precalculating free surface elevation from " + std::to_string(free_surface_time_sampled_.front()) +
                     " to " + std::to_string(free_surface_time_sampled_.back()) + "."
              << std::endl;

    // Calculate the free surface elevation
    free_surface_elevation_sampled_ =
        FreeSurfaceElevation(spectrum_frequencies_, spectral_densities_, time_array, sim_data_.water_depth, seed_);

    // Apply ramp if ramp_duration is greater than 0
    if (ramp_duration_ > 0.0) {
        // UpdateRampTimesteps();
        int ramp_timesteps   = static_cast<int>(ramp_duration_ / simulation_dt_) + 1;
        Eigen::VectorXd ramp = Eigen::VectorXd::LinSpaced(ramp_timesteps, 0.0, 1.0);

        for (size_t i = 0; i < ramp.size(); ++i) {
            free_surface_elevation_sampled_[i] *= ramp[i];
        }
    }

    // Open a file stream for writing
    std::ofstream eta_output("eta.txt");

    // Check if the file stream is open
    if (eta_output.is_open()) {
        // Write the spectral densities and their corresponding frequencies to the file
        for (size_t i = 0; i < free_surface_elevation_sampled_.size(); ++i) {
            eta_output << free_surface_time_sampled_[i] << " : " << free_surface_elevation_sampled_[i] << std::endl;
        }
        // Close the file stream
        eta_output.close();
    } else {
        std::cerr << "Unable to open file for writing." << std::endl;
    }

    std::cout << "Finished precalculating free surface elevation." << std::endl;
}

double IrregularWaves::ExcitationConvolution(int body, int dof, double time) {
    double f_ex           = 0.0;
    auto& irf_time_array  = ex_irf_time_sampled_[body];
    auto& irf_val_mat     = ex_irf_sampled_[body];
    auto& irf_width_array = ex_irf_width_sampled_[body];

    // asumptions: irf_time_array in ascending order, free_surface_time_sampled_ in ascending order
    // get initial index
    auto tmin    = free_surface_time_sampled_.front();
    auto tmax    = free_surface_time_sampled_.back();
    double t_tau = time - irf_time_array[0];
    int idx      = 0;
    if (t_tau <= tmin) {
        idx = 0;
    } else if (t_tau >= tmax) {
        idx = free_surface_time_sampled_.size() - 2;
    } else {
        idx = get_lower_index(t_tau, free_surface_time_sampled_);
    }

    // loop for all irf time values
    for (size_t j = 0; j < irf_time_array.size(); ++j) {
        double tau   = irf_time_array[j];
        double t_tau = time - tau;
        if (tmin <= t_tau && t_tau <= tmax) {
            // find next index if current lower bound > t_tau
            while (free_surface_time_sampled_[idx] > t_tau) {
                idx -= 1;
            }

            // free surface time values
            auto t1 = free_surface_time_sampled_[idx];
            auto t2 = free_surface_time_sampled_[idx + 1];

            // get free surface elevation
            double eta_val;
            if (t_tau == t1) {
                eta_val = free_surface_time_sampled_[idx];
            } else if (t_tau == t2) {
                eta_val = free_surface_time_sampled_[idx + 1];
            } else if (t_tau > t1 && t_tau < t2) {
                // linearly interpolate free surface elevation between bounds
                auto eta1 = free_surface_elevation_sampled_[idx];
                auto eta2 = free_surface_elevation_sampled_[idx + 1];
                // weights
                auto w1 = (t2 - t_tau) / (t2 - t1);
                auto w2 = 1.0 - w1;
                // weighted value
                auto eta_val = w1 * eta1 + w2 * eta2;
            } else {
                throw std::runtime_error("Excitation convolution: wrong tau value " + std::to_string(tau) +
                                         " not between " + std::to_string(t1) + " and " + std::to_string(t2) + ".");
            }

            // add to excitation force
            f_ex += irf_val_mat(dof, j) * eta_val * irf_width_array[j];

        } else {
            // throw error if trying to compute convolution after the maximum precomputed free elevation time
            throw std::runtime_error(
                "Excitation convolution: trying to find free surface elevation at a time out of bounds from the "
                "precomputed free surface elevation (" +
                std::to_string(t_tau) + "not in [" + std::to_string(tmin) + ", " + std::to_string(tmax) +
                "]). Excitation force ignored at this time step.");
        }
    }

    return f_ex;
}

void IrregularWaves::SetUpWaveMesh(std::string filename) {
    mesh_file_name_            = filename;
    int num_timesteps          = static_cast<int>(simulation_duration_ / simulation_dt_) + 1;
    Eigen::VectorXd time_index = Eigen::VectorXd::LinSpaced(num_timesteps, 0, simulation_duration_);
    std::vector<std::array<double, 3>> free_surface_3d_pts =
        CreateFreeSurface3DPts(free_surface_elevation_sampled_, time_index);
    std::vector<std::array<size_t, 3>> free_surface_triangles = CreateFreeSurfaceTriangles(time_index.size());

    WriteFreeSurfaceMeshObj(free_surface_3d_pts, free_surface_triangles, mesh_file_name_);
}

std::string IrregularWaves::GetMeshFile() {
    return mesh_file_name_;
}

Eigen::Vector3<double> IrregularWaves::GetWaveMeshVelocity() {
    return Eigen::Vector3d(1.0, 0, 0);
}