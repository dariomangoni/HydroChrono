#include <hydroc/hydro_forces.h>

#include <hydroc/helper.h>

#ifdef HYDROCHRONO_HAVE_IRRLICHT
	#include "chrono_irrlicht/ChVisualSystemIrrlicht.h"
	#include "chrono_irrlicht/ChIrrMeshTools.h"
	// Use the main namespaces of Irrlicht
	using namespace irr;
	using namespace irr::core;
	using namespace irr::scene;
	using namespace irr::video;
	using namespace irr::io;
	using namespace irr::gui;
	using namespace chrono::irrlicht;
#endif


#include <chrono/core/ChRealtimeStep.h>

#include <iomanip> // std::setprecision
#include <chrono> // std::chrono::high_resolution_clock::now
#include <vector> // std::vector<double>

// Use the namespaces of Chrono
using namespace chrono;
using namespace chrono::geometry;


#ifdef HYDROCHRONO_HAVE_IRRLICHT
class MyActionReceiver : public IEventReceiver {
public:
	MyActionReceiver(ChVisualSystemIrrlicht* vsys, bool& buttonPressed)
		: pressed(buttonPressed) {
		// store pointer application
		vis = vsys;

		// ..add a GUI button to control pause/play
		pauseButton = vis->GetGUIEnvironment()->addButton(rect<s32>(510, 20, 650, 35));
		buttonText = vis->GetGUIEnvironment()->addStaticText(L"Paused", rect<s32>(560, 20, 600, 35), false);
	}

	bool OnEvent(const SEvent& event) {
		// check if user clicked button
		if (event.EventType == EET_GUI_EVENT) {
			switch (event.GUIEvent.EventType) {
			case EGET_BUTTON_CLICKED:
				pressed = !pressed;
				if (pressed) {
					buttonText->setText(L"Playing");
				}
				else {
					buttonText->setText(L"Paused");
				}
				return pressed;
				break;
			default:
				break;
			}
		}
		return false;
	}

private:
	ChVisualSystemIrrlicht* vis;
	IGUIButton* pauseButton;
	IGUIStaticText* buttonText;

	bool& pressed;
};
#endif


int main(int argc, char* argv[]) {

	GetLog() << "Chrono version: " << CHRONO_VERSION << "\n\n";

    if (hydroc::setInitialEnvironment(argc, argv) != 0) {
        return 1;
    }

    std::filesystem::path DATADIR(hydroc::getDataDir());

	auto body1_meshfame = (DATADIR / "rm3" / "geometry" /"float_cog.obj")
		.lexically_normal()
		.generic_string();
	auto body2_meshfame = (DATADIR / "rm3" / "geometry" /"plate_cog.obj")
		.lexically_normal()
		.generic_string();		
	auto h5fname = (DATADIR / "rm3" / "hydroData" /"rm3.h5")
		.lexically_normal()
		.generic_string();

	// system/solver settings
	ChSystemNSC system;
	system.Set_G_acc(ChVector<>(0.0, 0.0, -9.81));
	double timestep = 0.01;
	//system.SetSolverType(ChSolver::Type::GMRES);
	system.SetTimestepperType(ChTimestepper::Type::HHT);
	system.SetSolverType(ChSolver::Type::GMRES);
	system.SetSolverMaxIterations(300);  // the higher, the easier to keep the constraints satisfied.
	system.SetStep(timestep);
	ChRealtimeStepTimer realtime_timer;
	double simulationDuration = 300.0;

	// some io/viz options
	bool visualizationOn = true;
	bool profilingOn = false;
	bool saveDataOn = true;
	std::vector<double> time_vector;
	std::vector<double> float_heave_position;
	std::vector<double> plate_heave_position;


	// set up body from a mesh
	std::cout << "Attempting to open mesh file: " << body1_meshfame << std::endl;
	std::shared_ptr<ChBody> float_body1 = chrono_types::make_shared<ChBodyEasyMesh>(                   //
		body1_meshfame,
		0,                                                                                        // density
		false,                                                                                    // do not evaluate mass automatically
		true,                                                                                     // create visualization asset
		false                                                                                     // collisions
		);

	std::cout << "Attempting to open mesh file: " << body2_meshfame << std::endl;
	std::shared_ptr<ChBody> plate_body2 = chrono_types::make_shared<ChBodyEasyMesh>(                   //
		body2_meshfame, 
		0,                                                                                        // density
		false,                                                                                    // do not evaluate mass automatically
		true,                                                                                     // create visualization asset
		false                                                                                     // collisions
		);

	// define the float's initial conditions
	system.Add(float_body1);
	float_body1->SetNameString("body1"); 
	float_body1->SetPos(ChVector<>(0, 0, (-0.72+0.1)));
	float_body1->SetMass(725834);
	float_body1->SetInertiaXX(ChVector<>(20907301.0, 21306090.66, 37085481.11));
	//float_body1->SetCollide(false);

	// define the plate's initial conditions
	system.Add(plate_body2);
	plate_body2->SetNameString("body2");
	plate_body2->SetPos(ChVector<>(0, 0, (-21.29)));
	plate_body2->SetMass(886691);
	plate_body2->SetInertiaXX(ChVector<>(94419614.57, 94407091.24, 28542224.82));
	//plate_body2->SetCollide(false);

	// add prismatic joint between the two bodies
	auto prismatic = chrono_types::make_shared<ChLinkLockPrismatic>();
	prismatic->Initialize(float_body1, plate_body2, false, ChCoordsys<>(ChVector<>(0, 0, -0.72)), ChCoordsys<>(ChVector<>(0, 0, -21.29)));
	system.AddLink(prismatic);

	auto prismatic_pto = chrono_types::make_shared<ChLinkTSDA>();
	prismatic_pto->Initialize(float_body1, plate_body2, false, ChVector<>(0, 0, -0.72), ChVector<>(0, 0, -21.29));
	prismatic_pto->SetDampingCoefficient(0.0);
	system.AddLink(prismatic_pto);

	// define wave parameters (not used in this demo)
	HydroInputs my_hydro_inputs;
	my_hydro_inputs.mode = WaveMode::noWaveCIC;// or 'regular' or 'regularCIC' or 'irregular';
	//my_hydro_inputs.regular_wave_amplitude = 0.022;
	//my_hydro_inputs.regular_wave_omega = 2.10;

	// attach hydrodynamic forces to body
	std::vector<std::shared_ptr<ChBody>> bodies;
	bodies.push_back(float_body1);
	bodies.push_back(plate_body2);
	TestHydro blah(bodies, h5fname, my_hydro_inputs);

	//// Debug printing added mass matrix and system mass matrix
	//ChSparseMatrix M;
	//system.GetMassMatrix(&M);
	//std::cout << M << std::endl;

	// for profiling
	auto start = std::chrono::high_resolution_clock::now();

#ifdef HYDROCHRONO_HAVE_IRRLICHT
	if (visualizationOn) {

		// create the irrlicht application for visualizing
		auto irrlichtVis = chrono_types::make_shared<ChVisualSystemIrrlicht>();
		irrlichtVis->AttachSystem(&system);
		irrlichtVis->SetWindowSize(1280, 720);
		irrlichtVis->SetWindowTitle("RM3 - Decay Test");
		irrlichtVis->SetCameraVertical(CameraVerticalDir::Z);
		irrlichtVis->Initialize();
		irrlichtVis->AddLogo();
		irrlichtVis->AddSkyBox();
		irrlichtVis->AddCamera(ChVector<>(0, -50, -10), ChVector<>(0, 0, -10)); // camera position and where it points
		irrlichtVis->AddTypicalLights();

		// add play/pause button
		bool buttonPressed = false;
		MyActionReceiver receiver(irrlichtVis.get(), buttonPressed);
		irrlichtVis->AddUserEventReceiver(&receiver);

		// main simulation loop
		while (irrlichtVis->Run() && system.GetChTime() <= simulationDuration) {
			irrlichtVis->BeginScene();
			irrlichtVis->Render();
			irrlichtVis->EndScene();
			if (buttonPressed) {
				//system.GetMassMatrix(&M);
				//std::cout << M << std::endl;
				// step the simulation forwards
				system.DoStepDynamics(timestep);
				// append data to std vector
				time_vector.push_back(system.GetChTime());
				float_heave_position.push_back(float_body1->GetPos().z());
				plate_heave_position.push_back(plate_body2->GetPos().z());
				// force playback to be real-time
				realtime_timer.Spin(timestep);
			}
		}
	}
	else {
#endif // #ifdef HYDROCHRONO_HAVE_IRRLICHT
		int frame = 0;
		while (system.GetChTime() <= simulationDuration) {
			// append data to std vector
			time_vector.push_back(system.GetChTime());
			float_heave_position.push_back(float_body1->GetPos().z());
			plate_heave_position.push_back(plate_body2->GetPos().z());
			// step the simulation forwards
			system.DoStepDynamics(timestep);

			frame++;
		}
#ifdef HYDROCHRONO_HAVE_IRRLICHT
	}
#endif

	// for profiling
	auto end = std::chrono::high_resolution_clock::now();
	unsigned duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

	if (profilingOn) {
		std::ofstream profilingFile;
		profilingFile.open("./results/rm3/decay/duration_ms.txt");
		if (!profilingFile.is_open()) {
			if (!std::filesystem::exists("./results/rm3/decay")) {
				std::cout << "Path " << std::filesystem::absolute("./results/rm3/decay") << " does not exist, creating it now..." << std::endl;
				std::filesystem::create_directory("./results");
				std::filesystem::create_directory("./results/rm3");
				std::filesystem::create_directory("./results/rm3/decay");
				profilingFile.open("./results/rm3/decay/duration_ms.txt");
				if (!profilingFile.is_open()) {
					std::cout << "Still cannot open file, ending program" << std::endl;
					return 0;
				}
			}
		}
		profilingFile << duration << "\n";
		profilingFile.close();
	}

	if (saveDataOn) {
		std::ofstream outputFile;
		outputFile.open("./results/rm3/decay/rm3_decay.txt");
		if (!outputFile.is_open()) {
			if (!std::filesystem::exists("./results/rm3/decay")) {
				std::cout << "Path " << std::filesystem::absolute("./results/rm3/decay") << " does not exist, creating it now..." << std::endl;
				std::filesystem::create_directory("./results");
				std::filesystem::create_directory("./results/rm3");
				std::filesystem::create_directory("./results/rm3/decay");
				outputFile.open("./results/rm3/decay/rm3_decay.txt");
				if (!outputFile.is_open()) {
					std::cout << "Still cannot open file, ending program" << std::endl;
					return 0;
				}
			}
		}
		outputFile << std::left << std::setw(10) << "Time (s)"
			<< std::right << std::setw(16) << "Float Heave (m)"
			<< std::right << std::setw(16) << "Plate Heave (m)"
			<< std::endl;
		for (int i = 0; i < time_vector.size(); ++i)
			outputFile << std::left << std::setw(10) << std::setprecision(2) << std::fixed << time_vector[i]
			<< std::right << std::setw(16) << std::setprecision(8) << std::fixed << float_heave_position[i]
			<< std::right << std::setw(16) << std::setprecision(8) << std::fixed << plate_heave_position[i]
			<< std::endl;
		outputFile.close();
	}
	return 0;
}