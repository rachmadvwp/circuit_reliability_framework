 /**
 * @file golden_nd_parameters_sweep_simulation.hpp
 *
 * @date Created on: March 14, 2016
 *
 * @author Author: Fernando García <fgarcia@die.upm.es> <fernando.garca@gmail.com>
 *
 * @section DESCRIPTION
 *
 * This Class is derived from SpectreSimulation
 * and is responsible for the simple sweep simulation mode.
 *
 */

// math
#include <cmath>
// Boost
#include "boost/filesystem.hpp" // includes all needed Boost.Filesystem declarations
// Radiation simulator
#include "golden_nd_parameters_sweep_simulation.hpp"
// #include "../simulation_results/results_summary_critical_parameter_1d_parameters_sweep.hpp"
#include "../../io_handling/raw_format_processor.hpp"
// constants
#include "../../global_functions_and_constants/gnuplot_constants.hpp"
#include "../../global_functions_and_constants/global_template_functions.hpp"
#include "../../global_functions_and_constants/global_constants.hpp"
#include "../../global_functions_and_constants/files_folders_io_constants.hpp"

GoldenNDParametersSweepSimulation::GoldenNDParametersSweepSimulation() {
	this->max_parallel_profile_instances = 2;
	this->children_correctly_simulated = false;
}

GoldenNDParametersSweepSimulation::~GoldenNDParametersSweepSimulation(){
	#ifdef DESTRUCTORS_VERBOSE
		std::cout <<  "GoldenNDParametersSweepSimulation destructor. direction:" + number2String(this) << std::endl;
	#endif
	// not required
	// delete golden_simulations_vector;
	// deleteContentsOfVectorOfPointers( golden_simulations_vector );
}

void GoldenNDParametersSweepSimulation::RunSpectreSimulation( ){
	if (!TestSetUp()){
		log_io->ReportError2AllLogs( "RunSpectreSimulation had not been previously set up. ");
		return;
	}
	// Environment
	ConfigureEnvironmentVariables();
	ShowEnvironmentVariables();
	// magnitudes_structure
	// in 1d-> vector of vectors? ok
	std::vector<std::vector<Magnitude*>*>* gms = new std::vector<std::vector<Magnitude*>*>();
	std::vector<std::string>* fs = new std::vector<std::string>();

	golden_magnitudes_structure = new NDMagnitudesStructure();
	golden_magnitudes_structure->set_magnitudes_structure(gms);
	golden_magnitudes_structure->set_files_structure(fs);
	// main threads group
	boost::thread_group mainTG;
	// params to be sweeped
	std::vector<SimulationParameter*> parameters2sweep;
	// Threads Creation
	for(auto const &p : *simulation_parameters ){
		if( p->get_allow_sweep() ){
			// Init parameter sweep (increments etc)
			p->InitSweep();
			parameters2sweep.push_back( p );
		}
	}
	 // go through every possible parameter combination
	unsigned int totalThreads = 1;
	for( auto const &p : parameters2sweep ){
		totalThreads = totalThreads*p->get_sweep_steps_number();
	}
	log_io->ReportThread( "Total threads to be simulated: " + number2String(totalThreads) + ". Max number of sweep threads: " + number2String(max_parallel_profile_instances), 1 );
	// parallel threads control
	unsigned int runningThreads = 0;
	unsigned int threadsCount = 0;
	// current parameter sweep indexes
	std::vector<unsigned int> parameterCountIndexes(parameters2sweep.size(), 0);
	// results
	golden_simulations_vector.ReserveSimulationsInMemory( totalThreads );
	golden_simulations_vector.set_group_name("golden_n-dimensional_analysis");
	// Threads Creation
	while( threadsCount<totalThreads ){
		// wait for resources
		WaitForResources( runningThreads, max_parallel_profile_instances, mainTG );
		// unicore
		// RunProfile( parameterCountIndexes, parameters2sweep, threadsCount++, *gms, *fs );
		// concurrent: ref vs resource copy issues in nd-crit sim type
		GoldenSimulation* pGS = CreateProfile(parameterCountIndexes, boost::ref(parameters2sweep), threadsCount);
		golden_simulations_vector.AddSpectreSimulation( pGS );
		mainTG.add_thread( new boost::thread(&GoldenNDParametersSweepSimulation::RunProfile,
			this, boost::ref(pGS), boost::ref(*gms), boost::ref(*fs) ));
		// update variables
		++threadsCount;
		++runningThreads;
		UpdateParameterSweepIndexes( parameterCountIndexes, parameters2sweep);
	}
	mainTG.join_all();
	// fgarcia
	log_io->ReportPlain2Log( "GoldenNDParametersSweepSimulation: mainTG.join_all()" );
	// check if every simulation ended correctly
	correctly_simulated = golden_simulations_vector.CheckCorrectlySimulated();
	children_correctly_simulated = correctly_simulated;
	log_io->ReportPlain2Log( "END OF GoldenNDParametersSweepSimulation::RunSpectreSimulation" );
}

GoldenSimulation* GoldenNDParametersSweepSimulation::CreateProfile(
	const std::vector<unsigned int>& parameterCountIndexes,
	std::vector<SimulationParameter*>& parameters2sweep, const unsigned int threadNumber ){
	// Create folder
	std::string s_threadNumber = number2String(threadNumber);
	std::string currentFolder = folder + kFolderSeparator
		+ "param_profile_" + s_threadNumber;
	if( !CreateFolder(currentFolder, true) ){
		log_io->ReportError2AllLogs( k2Tab + "-> Error creating folder '" + currentFolder + "'." );
		log_io->ReportError2AllLogs( "Error running profile" );
		return nullptr;
	}
	// copy only files to folder
	// find . -maxdepth 1 -type f -exec cp {} destination_path \;
	std::string copyNetlists0 = "find ";
	std::string copyNetlists1 = " -maxdepth 1 -type f -exec cp {} ";
	std::string copyNetlists2 = " \\;";
	std::string copyNetlists;
	copyNetlists = copyNetlists0 + folder + copyNetlists1 + currentFolder + copyNetlists2;
	if( std::system( copyNetlists.c_str() ) > 0){
		log_io->ReportError2AllLogs( k2Tab + "-> Error while copying netlist to '" + currentFolder + "'." );
		log_io->ReportError2AllLogs( "Error running sweep" );
		return nullptr;
	}
	// create thread
	GoldenSimulation* pGS = CreateGoldenSimulation( currentFolder, parameterCountIndexes, parameters2sweep, threadNumber);
	if( pGS==nullptr ){
		log_io->ReportError2AllLogs( "pGS is nullptr" );
		return nullptr;
	}
	return pGS;
}

void GoldenNDParametersSweepSimulation::RunProfile(
	GoldenSimulation* pGS,
	std::vector<std::vector<Magnitude*>*>& sweepVectorOfMagnitudesVector,
	std::vector<std::string>& sweepVectorOfFiles ){

	// run threadNumber
	pGS->RunSpectreSimulation();
	#ifdef SPECTRE_SIMULATIONS_VERBOSE
		log_io->ReportThread( "Golden thread: #" + pGS->get_simulation_id() , 3 );
	#endif
	// obtain magnitudes and file references
	sweepVectorOfMagnitudesVector.push_back(pGS->get_processed_magnitudes());
	sweepVectorOfFiles.push_back( pGS->get_singular_results_path());
	// end
	#ifdef RESULTS_ANALYSIS_VERBOSE
	log_io->ReportPlainStandard( k2Tab + "Ended thread " + pGS->get_simulation_id() );
	#endif
}

GoldenSimulation* GoldenNDParametersSweepSimulation::CreateGoldenSimulation(
		const std::string currentFolder, const std::vector<unsigned int>& parameterCountIndexes,
		const std::vector<SimulationParameter*>& parameters2sweep, const int ndIndex ){
	std::string s_ndIndex = number2String(ndIndex);
	// Simulation
	GoldenSimulation* pGS = new GoldenSimulation();
	// pGS->set_parameter_index( paramIndex );
	// pGS->set_sweep_index( sweepIndex );
	pGS->set_is_nested_simulation( true );
	pGS->set_simulation_id(  "golden_param_profile_" + s_ndIndex );
	pGS->set_n_dimensional(true);
	pGS->set_n_d_profile_index(ndIndex);
	pGS->set_log_io( log_io );
	pGS->set_altered_scenario_index( altered_scenario_index );
	// pGS->set_golden_magnitudes_structure( golden_magnitudes_structure );
	// Spectre command and args
	pGS->set_spectre_command( spectre_command );
	pGS->set_pre_spectre_command( pre_spectre_command );
	pGS->set_post_spectre_command( post_spectre_command );
	pGS->set_spectre_command_log_arg( spectre_command_log_arg );
	pGS->set_spectre_command_folder_arg( spectre_command_folder_arg );
	// Spectre environment variables
	pGS->set_magnitudes_2be_found( magnitudes_2be_found );
	pGS->set_ahdl_simdb_env( ahdl_simdb_env );
	pGS->set_ahdl_shipdb_env( ahdl_shipdb_env );
	pGS->set_top_folder( top_folder );
	pGS->set_folder( currentFolder );
	pGS->set_altered_statement_path( altered_statement_path );
	pGS->set_save_spectre_transients( save_spectre_transients );
	pGS->set_save_processed_transients( save_processed_transients );
	pGS->set_plot_transients( plot_transients );
	pGS->set_main_analysis( main_analysis );
	pGS->set_main_transient_analysis( main_transient_analysis );
	pGS->set_process_magnitudes( true );
	pGS->set_export_processed_magnitudes( true );
	pGS->set_export_magnitude_errors( export_magnitude_errors );
	// copy of simulation_parameters
	pGS->CopySimulationParameters( *simulation_parameters );
	// update parameter values
	unsigned int sweepedParamIndex = 0;
	for( auto const &p : parameters2sweep ){
		// if parameter value is fixed for golden scenario, whe use the defualt value
		if( !p->get_golden_fixed() ){
			// update val only when required,
			if( !pGS->UpdateParameterValue( *p, number2String( p->GetSweepValue( parameterCountIndexes.at(sweepedParamIndex)) ) ) ){
				log_io->ReportError2AllLogs( p->get_name()
					+ " not found in CriticalParameterValueSimulation and could not be updated." );
			}
		}
		++sweepedParamIndex;
	}
	return pGS;
}

bool GoldenNDParametersSweepSimulation::TestSetUp(){
	if( simulation_parameters==nullptr ){
		log_io->ReportError2AllLogs( "nullptr parameter_critical_value");
		return false;
	}else if(folder.compare("")==0){
		log_io->ReportError2AllLogs( "nullptr folder ");
		return false;
	}
	return true;
}

NDMagnitudesStructure* GoldenNDParametersSweepSimulation::GetGoldenMagnitudes(){
	NDMagnitudesStructure* gms = new NDMagnitudesStructure(*golden_magnitudes_structure);
	return gms;
}
