#include "stdafx.h"
#include "analyzeFP.hpp"
#include <curl/curl.h>
#include <future>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool blink, debugMode, validVersion, autoLoad;

vector<int> timedata;

size_t failPos, relCount;

std::future<void> fut;

using namespace std;
using namespace EuroScopePlugIn;

// Run on Plugin Initialization
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
	blink = false;
	debugMode = false;
	validVersion = true; //Reset in first timer call
	autoLoad = true;

	failPos = 0;
	relCount = 0;

	timedata = { 0, 0, 0 };

	string loadingMessage = "Loading complete. Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += ".";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	if (validVersion) {
		RegisterTagItemType("VFPC", TAG_ITEM_FPCHECK);
		RegisterTagItemFunction("Show Checks", TAG_FUNC_CHECKFP_MENU);
	}
}

// Run on Plugin destruction, Ie. Closing EuroScope or unloading plugin
CVFPCPlugin::~CVFPCPlugin()
{
}

/*
	Custom Functions
*/

/*size_t CVFPCPlugin::WriteFunction(void *contents, size_t size, size_t nmemb, void *out)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
	((std::string*)out)->append(reinterpret_cast<char*>(contents) + '\0', size * nmemb);
	return size * nmemb;
}*/

static size_t curlCallback(void *contents, size_t size, size_t nmemb, void *outString)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
 	((std::string*)outString)->append(reinterpret_cast<char*>(contents), size * nmemb);
	return size * nmemb;
}

void CVFPCPlugin::debugMessage(string type, string message) {
	// Display Debug Message if debugMode = true
	if (debugMode) {
		DisplayUserMessage("VFPC Log", type.c_str(), message.c_str(), true, true, true, false, false);
	}
}

void CVFPCPlugin::sendMessage(string type, string message) {
	// Show a message
	DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, true, false);
}

void CVFPCPlugin::sendMessage(string message) {
	DisplayUserMessage("VFPC", "System", message.c_str(), true, true, true, false, false);
}

void CVFPCPlugin::timeCall() {
	Document doc;
	CURL* curl = curl_easy_init();
	string url = "http://worldtimeapi.org/api/timezone/Europe/London";

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	uint64_t httpCode = 0;
	std::string readBuffer;

	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	//curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (httpCode == 200)
	{
		if (doc.Parse<0>(readBuffer.c_str()).HasParseError())
		{
			sendMessage("An error occurred whilst reading date/time data. The plugin will not automatically attempt to reload from the API. To restart data fetching, type \".vfpc load\".");
			debugMessage("Error", str(boost::format("Config Parse: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

			doc.Parse<0>("{}");
		}
	}
	else
	{
		sendMessage("An error occurred whilst downloading date/time data. The plugin will not automatically attempt to reload from the API. Check your connection and restart data fetching by typing \".vfpc load\".");
		debugMessage("Error", str(boost::format("Config Download: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

		doc.Parse<0>("{}");
	}

	if (doc.HasMember("datetime") && doc["datetime"].IsString() && doc.HasMember("day_of_week") && doc["day_of_week"].IsInt()) {
		string hour = ((string)doc["datetime"].GetString()).substr(11, 2);
		string min = ((string)doc["datetime"].GetString()).substr(14, 2);

		timedata[0] = doc["day_of_week"].GetInt();
		timedata[1] = stoi(hour);
		timedata[2] = stoi(min);
	}
}

void CVFPCPlugin::webCall(string endpoint, Document& out) {
	CURL* curl = curl_easy_init();
	string url = MY_API_ADDRESS + endpoint;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	uint64_t httpCode = 0;
	std::string readBuffer;

	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	//curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (httpCode == 200)
	{
		if (out.Parse<0>(readBuffer.c_str()).HasParseError())
		{
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload from the API. To restart data fetching, type \".vfpc load\".");
			debugMessage("Error", str(boost::format("Config Parse: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

			out.Parse<0>("[{\"Icao\": \"XXXX\"}]");
		}
	}
	else
	{
		sendMessage("An error occurred whilst downloading data. The plugin will not automatically attempt to reload from the API. Check your connection and restart data fetching by typing \".vfpc load\".");
		debugMessage("Error", str(boost::format("Config Download: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

		out.Parse<0>("[{\"Icao\": \"XXXX\"}]");
	}
}

bool CVFPCPlugin::checkVersion() {
	Document version;
	webCall("version", version);
	
	if (version.HasMember("VFPC_Version") && version["VFPC_Version"].IsString()) {
		vector<string> current = split(version["VFPC_Version"].GetString(), '.');
		vector<string> installed = split(MY_PLUGIN_VERSION, '.');

		if ((installed[0] > current[0]) || //Major version higher
			(installed[0] == current[0] && installed[1] > current[1]) || //Minor version higher
			(installed[0] == current[0] && installed[1] == current[1] && installed[2] >= current[2])) { //Revision higher
			return true;
		}
		else {
			sendMessage("Update available - the plugin has been disabled. Please update and reload the plugin to continue. (Note: .vfpc load will NOT work.)");
		}
	}
	else {
		sendMessage("Failed to check for updates - the plugin has been disabled. If no updates are available, please unload and reload the plugin to try again. (Note: .vfpc load will NOT work.)");
	}

	return false;
}

void CVFPCPlugin::getSids() {
	webCall("mongoFull", config);

	airports.clear();

	for (SizeType i = 0; i < config.Size(); i++) {
		const Value& airport = config[i];
		string airport_icao = airport["icao"].GetString();

		airports.insert(pair<string, SizeType>(airport_icao, i));
	}
}

// Does the checking and magic stuff, so everything will be alright when this is finished! Or not. Who knows?
vector<vector<string>> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	//out[0] = Normal Output, out[1] = Debug Output
	vector<vector<string>> returnOut = { vector<string>(), vector<string>() }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Syntax, 8 = Passed/Failed

	returnOut[0].push_back(flightPlan.GetCallsign());
	returnOut[1].push_back(flightPlan.GetCallsign());
	for (int i = 1; i < 10; i++) {
		returnOut[0].push_back("-");
		returnOut[1].push_back("-");
	}

	string origin = flightPlan.GetFlightPlanData().GetOrigin(); boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination(); boost::to_upper(destination);
	SizeType origin_int;
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();

	vector<string> route = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (size_t i = 0; i < route.size(); i++) {
		boost::to_upper(route[i]);
	}

	// Remove Speed/Alt Data From Route
	regex lvl_chng("(N|M)[0-9]{3,4}(A|F)[0-9]{3}$");

	// Remove "DCT" And Speed/Level Change Instances from Route
	for (size_t i = 0; i < route.size(); i++) {
		int count = 0;
		size_t pos = 0;

		for (size_t j = 0; j < route[i].size(); j++) {
			if (route[i][j] == '/') {
				count++;
				pos = j;
			}
		}

		switch (count) {
			case 0:
			{
				break;
			}
			case 2:
			{
				size_t first_pos = route[i].find('/');
				route[i] = route[i].substr(first_pos, string::npos);
			}
			case 1:
			{
				if (route[i].size() > pos + 1 && regex_match((route[i].substr(pos + 1, string::npos)), lvl_chng)) {
					route[i] = route[i].substr(0, pos);
					break;
				}
				else {
					returnOut[0][8] = "Invalid Speed/Level Change";
					returnOut[0][9] = "Failed";

					returnOut[1][8] = "Invalid Route Item: " + route[i];
					returnOut[1][9] = "Failed";
					return returnOut;
				}
			}
			default:
			{
				returnOut[0][8] = "Invalid Syntax - Too Many \"/\" Characters in One or More Waypoints";
				returnOut[0][9] = "Failed";

				returnOut[1][8] = "Invalid Route Item: " + route[i];
				returnOut[1][9] = "Failed";
				return returnOut;
			}
		}
	}
	for (size_t i = 0; i < route.size(); i++) {
		if (route[i] == "DCT") {
			route.erase(route.begin() + i);
		}
	}

	//Remove Speed/Level Data From Start Of Route
	if (regex_match(route[0], lvl_chng)) {
		route.erase(route.begin());
	}

	string sid = flightPlan.GetFlightPlanData().GetSidName(); boost::to_upper(sid);

	// Remove any # characters from SID name
	boost::erase_all(sid, "#");

	// Flightplan has SID
	if (!sid.length()) {
		returnOut[0][1] = returnOut[1][1] = "Invalid SID - None Set";
		returnOut[0][9] = returnOut[1][9] = "Failed";
		return returnOut;
	}

	string first_wp = sid.substr(0, sid.find_first_of("0123456789"));
	if (0 != first_wp.length())
		boost::to_upper(first_wp);
	string sid_suffix;
	if (first_wp.length() != sid.length()) {
		sid_suffix = sid.substr(sid.find_first_of("0123456789"), sid.length());
		boost::to_upper(sid_suffix);
	}

	// Did not find a valid SID
	if (0 == sid_suffix.length() && "VCT" != first_wp) {
		returnOut[0][1] = returnOut[1][1] = "Invalid SID - None Set";
		returnOut[0][9] = returnOut[1][9] = "Failed";
		return returnOut;
	}

	// Check First Waypoint Correct. Remove SID References & First Waypoint From Route.
	bool success = false;
	bool stop = false;
	
	while (!stop) {
		size_t wp_size = first_wp.size();
		size_t entry_size = route[0].size();
		if (route[0].substr(0, wp_size) == first_wp) {
			//First Waypoint
			if (wp_size == entry_size) {
				success = true;
			}
			//3 or 5 Letter Waypoint SID - In Full
			else if (entry_size > wp_size && isdigit(route[0][wp_size])) {
				//SID Has Letter Suffix
				for (size_t i = wp_size + 1; i < entry_size; i++) {
					if (!isalpha(route[0][i])) {
						stop = true;
					}
				}
			}
			else {
				stop = true;
			}

			route.erase(route.begin());
		}
		//5 Letter Waypoint SID - Abbreviated to 6 Chars
		else if (wp_size == 5 && entry_size >= wp_size && isdigit(route[0][wp_size - 1])) {
			//SID Has Letter Suffix
			for (size_t i = wp_size; i < entry_size; i++) {
				if (!isalpha(route[0][i])) {
					stop = true;
				}
			}

			route.erase(route.begin());
		}
		else {
			stop = true;
		}
	}
	
	if (!success) {
		returnOut[0][1] = "Invalid SID - Route Not From Final SID Fix";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - Route must start at " + first_wp + ".";
		returnOut[1][9] = "Failed";
		return returnOut;
	}

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnOut[0][1] = "Invalid SID - Airport Not Found";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - " + origin + " not in database.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
	else
	{
		origin_int = airports[origin];
	}

	// Any SIDs defined
	if (!config[origin_int].HasMember("sids") || !config[origin_int]["sids"].IsArray()) {
		returnOut[0][1] = "Invalid SID - None Defined";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - " + origin + " exists in database but has no SIDs defined.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
	size_t pos = string::npos;

	for (size_t i = 0; i < config[origin_int]["sids"].Size(); i++) {
		if (config[origin_int]["sids"][i].HasMember("point") && !first_wp.compare(config[origin_int]["sids"][i]["point"].GetString()) && config[origin_int]["sids"][i].HasMember("constraints") && config[origin_int]["sids"][i]["constraints"].IsArray()) {
			pos = i;
		}
	}

	// Needed SID defined
	if (pos != string::npos) {
		const Value& sid_ele = config[origin_int]["sids"][pos];
		const Value& conditions = sid_ele["constraints"];

		int round = 0;

		bool sections[10]{};
		fill(begin(sections), end(sections), true);

		vector<bool> validity, new_validity;
		vector<string> results;
		bool restFails[3]{ 0 }; // 0 = Suffix, 1 = Aircraft/Engines, 2 = Date/Time Restrictions
		int Min, Max;
			
		while (round < 6) {
			new_validity = {};

			for (SizeType i = 0; i < conditions.Size(); i++) {
				if (round == 0 || validity[i]) {
					switch (round) {
					case 0:
					{
						//Destinations
						bool res = true;

						if (conditions[i]["nodests"].IsArray() && conditions[i]["nodests"].Size()) {
							string dest;
							if (destArrayContains(conditions[i]["nodests"], destination.c_str()).size()) {
								res = false;
							}
						}

						if (conditions[i]["dests"].IsArray() && conditions[i]["dests"].Size()) {
							string dest;
							if (!destArrayContains(conditions[i]["dests"], destination.c_str()).size()) {
								res = false;
							}
						}

						new_validity.push_back(res);
						break;
					}
					case 1:
					{
						//Route
						bool res = true;

						string perms = "";

						if (conditions[i].HasMember("route") && conditions[i]["route"].IsArray() && conditions[i]["route"].Size() && !routeContains(flightPlan.GetCallsign(), route, conditions[i]["route"])) {
							res = false;
						}

						if (conditions[i].HasMember("noroute") && res && conditions[i]["noroute"].IsArray() && conditions[i]["noroute"].Size() && routeContains(flightPlan.GetCallsign(), route, conditions[i]["noroute"])) {
							res = false;
						}

						new_validity.push_back(res);
						break;
					}
					case 2:
					{
						//Nav Perf
						if (conditions[i].HasMember("nav") && conditions[i]["nav"].IsString()) {
							string navigation_constraints(conditions[i]["nav"].GetString());
							if (string::npos == navigation_constraints.find_first_of(flightPlan.GetFlightPlanData().GetCapibilities())) {
								new_validity.push_back(false);
							}
							else {
								new_validity.push_back(true);
							}
						}
						else {
							new_validity.push_back(true);
						}
						break;
					}
					case 3:
					{
						bool res_minmax = true;

						//Min Level
						if (conditions[i].HasMember("min") && (Min = conditions[i]["min"].GetInt()) > 0 && (RFL / 100) <= Min) {
							res_minmax = false;
						}

						//Max Level
						if (conditions[i].HasMember("max") && (Max = conditions[i]["max"].GetInt()) > 0 && (RFL / 100) >= Max) {
							res_minmax = false;
						}

						new_validity.push_back(res_minmax);
						break;
					}
					case 4:
					{
						//Even/Odd Levels
						string direction = conditions[i]["dir"].GetString();
						boost::to_upper(direction);

						if (direction == "EVEN") {
							if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 2)) {
								new_validity.push_back(true);
							}
							else if (RFL <= 41000 && (RFL / 1000) % 2 == 0) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
							}
						}
						else if (direction == "ODD") {
							if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 0)) {
								new_validity.push_back(true);
							}
							else if (RFL <= 41000 && (RFL / 1000) % 2 == 1) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
							}
						}
						else { //(direction == "ANY")
							new_validity.push_back(true);
						}
						break;
					}
					case 5:
					{
						bool sidwide = true;
						bool res = true;
						// Restrictions Array
						if (conditions[i].HasMember("override") && conditions[i]["override"].IsBool() && conditions[i]["override"].GetBool()) {
							sidwide = false;
						}

						if (conditions[i]["restrictions"].IsArray() && conditions[i]["restrictions"].Size()) {
							res = false;
							for (size_t j = 0; j < conditions[i]["restrictions"].Size(); j++) {
								bool temp = true;

								if (conditions[i]["restrictions"][j]["types"].IsArray() && conditions[i]["restrictions"][j]["types"].Size()) {
									if (arrayContains(conditions[i]["restrictions"][j]["types"], flightPlan.GetFlightPlanData().GetEngineType()) ||
										arrayContains(conditions[i]["restrictions"][j]["types"], flightPlan.GetFlightPlanData().GetAircraftType())) {
										restFails[1] = true;
									}
									else {
										temp = false;
									}
								}

								if (conditions[i]["restrictions"][j]["suffix"].IsArray() && conditions[i]["restrictions"][j]["suffix"].Size()) {
									if (arrayContainsEnding(conditions[i]["restrictions"][j]["suffix"], sid_suffix)) {
										restFails[0] = true;
									}
									else {
										temp = false;
									}
								}

								if (conditions[i]["restrictions"][j].HasMember("start") && conditions[i]["restrictions"][j].HasMember("end")) {
									bool date = false;
									bool time = false;

									int startdate;
									int enddate;
									int* starttime = (int*)calloc(2, sizeof(int));
									int* endtime = (int*)calloc(2, sizeof(int));

									if (conditions[i]["restrictions"][j]["start"].HasMember("date")
										&& conditions[i]["restrictions"][j]["start"]["date"].IsInt()
										&& conditions[i]["restrictions"][j]["end"].HasMember("date")
										&& conditions[i]["restrictions"][j]["end"]["date"].IsInt()) {
										date = true;

										startdate = conditions[i]["restrictions"][j]["start"]["date"].GetInt();
										enddate = conditions[i]["restrictions"][j]["end"]["date"].GetInt();
									}

									if (conditions[i]["restrictions"][j]["start"].HasMember("time")
										&& conditions[i]["restrictions"][j]["start"]["time"].IsString()
										&& conditions[i]["restrictions"][j]["end"].HasMember("time")
										&& conditions[i]["restrictions"][j]["end"]["time"].IsString()) {
										time = true;

										string startstring = conditions[i]["restrictions"][j]["start"]["time"].GetString();
										string endstring = conditions[i]["restrictions"][j]["end"]["time"].GetString();

										starttime[0] = stoi(startstring.substr(0, 2));
										starttime[1] = stoi(startstring.substr(2, 2));
										endtime[0] = stoi(endstring.substr(0, 2));
										endtime[1] = stoi(endstring.substr(2, 2));
									}

									bool valid = true;

									if (date || time) {
										valid = false;

										if (!date && time) {
											if (starttime[0] > endtime[0] || (starttime[0] == endtime[0] && starttime[1] >= endtime[1])) {
												if (timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1]) || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1])) {
													valid = true;
												}
											}
											else {
												if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
													valid = true;
												}
											}
										}
										else if (startdate == enddate) {
											if (!time) {
												valid = true;
											}
											else if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
												valid = true;
											}
										}
										else if (startdate < enddate) {
											if (timedata[0] > startdate && timedata[0] < enddate) {
												valid = true;
											}
											else if (timedata[0] == startdate) {
												if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
													valid = true;
												}
											}
											else if (timedata[0] == enddate) {
												if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
													valid = true;
												}
											}
										}
										else if (startdate > enddate) {
											if (timedata[0] < startdate || timedata[0] > enddate) {
												valid = true;
											}
											else if (timedata[0] == startdate) {
												if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
													valid = true;
												}
											}
											else if (timedata[0] == enddate) {
												if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
													valid = true;
												}
											}
										}
									}

									if (valid) {
										restFails[2] = true;
									}
									else {
										temp = false;
									}
								}

								if (temp) {
									res = true;
								}
							}
						}

						if (sidwide && sid_ele["restrictions"].IsArray() && sid_ele["restrictions"].Size()) {
							for (size_t j = 0; j < sid_ele["restrictions"].Size(); j++) {
								bool temp = true;

								if (sid_ele["restrictions"][j]["types"].IsArray() && sid_ele["restrictions"][j]["types"].Size()) {
									if (arrayContains(sid_ele["restrictions"][j]["types"], flightPlan.GetFlightPlanData().GetEngineType()) ||
										arrayContains(sid_ele["restrictions"][j]["types"], flightPlan.GetFlightPlanData().GetAircraftType())) {
										restFails[1] = true;
									}
									else {
										temp = false;
									}
								}

								if (sid_ele["restrictions"][j]["suffix"].IsArray() && sid_ele["restrictions"][j]["suffix"].Size()) {
									if (arrayContainsEnding(sid_ele["restrictions"][j]["suffix"], sid_suffix)) {
										restFails[0] = true;
									}
									else {
										temp = false;
									}
								}

								if (sid_ele["restrictions"][j]["types"].IsArray() && sid_ele["restrictions"][j]["types"].Size() &&
									!arrayContains(sid_ele[j]["types"], flightPlan.GetFlightPlanData().GetEngineType()) &&
									!arrayContains(sid_ele["restrictions"][j]["types"], flightPlan.GetFlightPlanData().GetAircraftType())) {
									temp = false;
								}

								if (sid_ele["restrictions"][j]["suffix"].IsArray() && sid_ele["restrictions"][j]["suffix"].Size() &&
									!arrayContainsEnding(sid_ele["restrictions"][j]["suffix"], sid_suffix)) {
									temp = false;
								}

								if (sid_ele["restrictions"][j].HasMember("start") && sid_ele["restrictions"][j].HasMember("end")) {
									bool date = false;
									bool time = false;

									int startdate;
									int enddate;
									int* starttime = (int*)calloc(2, sizeof(int));
									int* endtime = (int*)calloc(2, sizeof(int));

									if (sid_ele["restrictions"][j]["start"].HasMember("date")
										&& sid_ele["restrictions"][j]["start"]["date"].IsInt()
										&& sid_ele["restrictions"][j]["end"].HasMember("date")
										&& sid_ele["restrictions"][j]["end"]["date"].IsInt()) {
										date = true;

										startdate = sid_ele["restrictions"][j]["start"]["date"].GetInt();
										enddate = sid_ele["restrictions"][j]["end"]["date"].GetInt();
									}

									if (sid_ele["restrictions"][j]["start"].HasMember("time")
										&& sid_ele["restrictions"][j]["start"]["time"].IsString()
										&& sid_ele["restrictions"][j]["end"].HasMember("time")
										&& sid_ele["restrictions"][j]["end"]["time"].IsString()) {
										time = true;

										string startstring = sid_ele["restrictions"][j]["start"]["time"].GetString();
										string endstring = sid_ele["restrictions"][j]["end"]["time"].GetString();

										starttime[0] = stoi(startstring.substr(0, 2));
										starttime[1] = stoi(startstring.substr(2, 2));
										endtime[0] = stoi(endstring.substr(0, 2));
										endtime[1] = stoi(endstring.substr(2, 2));
									}

									bool valid = true;

									if (date || time) {
										valid = false;

										if (!date && time) {
											if (starttime[0] > endtime[0] || (starttime[0] == endtime[0] && starttime[1] >= endtime[1])) {
												if (timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1]) || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1])) {
													valid = true;
												}
											}
											else {
												if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
													valid = true;
												}
											}
										}
										else if (startdate == enddate) {
											if (!time) {
												valid = true;
											}
											else if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
												valid = true;
											}
										}
										else if (startdate < enddate) {
											if (timedata[0] > startdate && timedata[0] < enddate) {
												valid = true;
											}
											else if (timedata[0] == startdate) {
												if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
													valid = true;
												}
											}
											else if (timedata[0] == enddate) {
												if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
													valid = true;
												}
											}
										}
										else if (startdate > enddate) {
											if (timedata[0] < startdate || timedata[0] > enddate) {
												valid = true;
											}
											else if (timedata[0] == startdate) {
												if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
													valid = true;
												}
											}
											else if (timedata[0] == enddate) {
												if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
													valid = true;
												}
											}
										}
									}

									if (valid) {
										restFails[2] = true;
									}
									else {
										temp = false;
									}
								}

								if (temp) {
									res = true;
								}
							}
						}

						new_validity.push_back(res);
						break;
					}
					}
				}
				else {
					new_validity.push_back(false);
				}
			}

			if (all_of(new_validity.begin(), new_validity.end(), [](bool v) { return !v; })) {
				break;
			}
			else {
				validity = new_validity;
				round++;
			}
		}

		returnOut[0][0] = flightPlan.GetCallsign();
		returnOut[1][0] = flightPlan.GetCallsign();
		for (size_t i = 1; i < returnOut[0].size(); i++) {
			returnOut[0][i] = "-";
			returnOut[1][i] = "-";
		}

		returnOut[0][9] = "Failed";
		returnOut[1][9] = "Failed";

		vector<size_t> successes{};

		for (size_t i = 0; i < validity.size(); i++) {
			if (validity[i]) {
				successes.push_back(i);
			}
		}

		switch (round) {
		case 6:
		{	
			returnOut[0][7] = "Passed Restrictions";
			returnOut[0][9] = "Passed";
			returnOut[1][7] = returnOut[0][7];
			returnOut[1][9] = returnOut[0][9];
		}
		case 5:
		{
			if (round == 5) {
				returnOut[0][7] = "Failed Restrictions";
				returnOut[1][7] = returnOut[0][7];
			}
			returnOut[0][6] = "Passed Level Direction.";
			returnOut[1][6] = "Passed " + DirectionOutput(origin_int, pos, successes) + ".";

			break;
		}
		case 4:
		{
			if (round == 4) {
				string res = "";
				returnOut[0][6] = "Failed " + DirectionOutput(origin_int, pos, successes) + ".";
				returnOut[1][6] = returnOut[0][6];
			}

			returnOut[0][5] = "Passed Min/Max Level.";	
			returnOut[1][5] = "Passed " + MinMaxOutput(origin_int, pos, successes);
		}
		case 3:
		{
			if (round == 3) {
				returnOut[0][5] = "Failed " + MinMaxOutput(origin_int, pos, successes);
				returnOut[1][5] = returnOut[0][5];
			}

			returnOut[0][4] = "Passed Navigation Performance.";
			returnOut[1][4] = "Passed " + NavPerfOutput(origin_int, pos, successes) + ".";
		}

		case 2:
		{
			if (round == 2) {
				returnOut[0][4] = "Failed " + NavPerfOutput(origin_int, pos, successes) + ".";
				returnOut[1][4] = returnOut[0][4];
			}

			returnOut[0][3] = "Passed Route.";
			returnOut[1][3] = "Passed " + RouteOutput(origin_int, pos, successes) + ".";
		}
		case 1:
		{
			if (round == 1) {
				returnOut[0][3] = "Failed " + RouteOutput(origin_int, pos, successes) + ".";
				returnOut[1][3] = returnOut[0][3];
			}

			returnOut[0][2] = "Passed Destination.";
			returnOut[1][2] = "Passed " + DestinationOutput(origin_int, pos, successes) + ".";
		}
		case 0:
		{
			if (round == 0) {
				returnOut[0][2] = "Failed " + DestinationOutput(origin_int, pos, successes) + ".";
				returnOut[1][2] = returnOut[0][2];
			}

			returnOut[0][1] = "Valid SID - " + sid + ".";
			returnOut[1][1] = returnOut[0][1]; // +" Contains Valid " + SuffixOutput(origin_int, pos, successes) + ".";
			break;
		}
		}

		return returnOut;
	}
	else {
		returnOut[0][1] = "Invalid SID - SID Not Found";
		returnOut[0][9] = "Failed";
		returnOut[1][1] = "Invalid SID - " + sid + " departure not in database.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
}

string CVFPCPlugin::RestrictionsOutput(size_t origin_int, size_t pos, vector<size_t> successes, bool rests[]) {

}

string CVFPCPlugin::DirectionOutput(size_t origin_int, size_t pos, vector<size_t> successes) {
	const Value& conditions = config[origin_int]["sids"][pos]["constraints"];
	bool lvls[2] { false, false };
	for (int each : successes) {
		if (conditions[each].HasMember("dir") && conditions[each]["dir"].IsString()) {
			string val = conditions[each]["dir"].GetString();
			if (val == "EVEN") {
				lvls[0] = true;
			}
			else if (val == "ODD") {
				lvls[1] = true;
			}
		}
		else {
			lvls[0] = true;
			lvls[1] = true;
		}
	}

	string out = "Level Direction. Required Direction: ";

	if (lvls[0] && lvls[1]) {
		out += "Any";
	}
	else if (lvls[0]) {
		out += "Even";
	}
	else if (lvls[1]) {
		out += "Odd";
	}
	else {
		out += "Any";
	}

	return out;
}

string CVFPCPlugin::MinMaxOutput(size_t origin_int, size_t pos, vector<size_t> successes) {
	const Value& conditions = config[origin_int]["sids"][pos]["constraints"];
	vector<vector<int>> raw_lvls{};
	for (int each : successes) {
		vector<int> lvls = { MININT, MAXINT };

		if (conditions[each].HasMember("min") && conditions[each]["min"].IsInt()) {
			lvls[0] = conditions[each]["min"].GetInt();
		}

		if (conditions[each].HasMember("max") && conditions[each]["max"].IsInt()) {
			lvls[1] = conditions[each]["max"].GetInt();
		}

		raw_lvls.push_back(lvls);
	}

	bool changed;
	size_t i = 0;

	while (i < raw_lvls.size() - 1) {
		for (size_t j = 0; j < raw_lvls.size(); j++) {
			if (i == j) {
				break;
			}
			//Item j is a subset of Item i
			if (raw_lvls[j][0] >= raw_lvls[i][0] && raw_lvls[j][1] <= raw_lvls[i][1]) {
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
			//Item j extends higher than Item i
			else if (raw_lvls[j][0] >= raw_lvls[i][0]) {
				raw_lvls[i][1] = raw_lvls[j][1];
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
			//Item j extends lower than Item i
			else if (raw_lvls[j][1] <= raw_lvls[i][1]) {
				raw_lvls[i][0] = raw_lvls[j][0];
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
		}

		if (!changed) {
			i++;
		}
	}

	string out = "Min/Max Level: ";

	for (vector<int> each : raw_lvls) {
		if (each[0] == MININT && each[1] == MAXINT) {
			out += "Any Level, ";
		}
		else if (each[0] == MININT) {
			out += to_string(each[1]) + "-, ";
		}
		else if (each[1] == MAXINT) {
			out += to_string(each[0]) + "+, ";

		}
		else {
			out += to_string(each[0]) + "-" + to_string(each[1]);
			out += ", ";
		}
	}

	out = out.substr(0, out.size() - 2) + ".";

	return out;
}

string CVFPCPlugin::NavPerfOutput(size_t origin_int, size_t pos, vector<size_t> successes) {
	const Value& conditions = config[origin_int]["sids"][pos]["constraints"];
	vector<string> navperf{};
	for (int each : successes) {
		if (conditions[each].HasMember("nav") && conditions[each]["nav"].IsString()) {
			navperf.push_back(string(conditions[each]["nav"].GetString()));
		}
	}

	sort(navperf.begin(), navperf.end());
	vector<string>::iterator itr = unique(navperf.begin(), navperf.end());
	navperf.erase(itr, navperf.end());

	string out = "";

	for (string each : navperf) {
		out += each + ", ";
	}

	if (out == "") {
		out = "None Specified";
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Navigation Performance. Required Performance: " + out;
}

string CVFPCPlugin::RouteOutput(size_t origin_int, size_t pos, vector<size_t> successes) {
	const Value& conditions = config[origin_int]["sids"][pos]["constraints"];
	vector<string> outroute{};
	for (int each : successes) {
		string out = "";
		if (conditions[each].HasMember("route") && conditions[each]["route"].IsArray() && conditions[each]["route"].Size()) {
			out += conditions[each]["route"][(SizeType)0].GetString();

			for (SizeType j = 1; j < conditions[each]["route"].Size(); j++) {
				out += " or ";
				out += conditions[each]["route"][j].GetString();
			}
		}

		if (conditions[each].HasMember("noroute") && conditions[each]["noroute"].IsArray() && conditions[each]["noroute"].Size()) {
			if (out != "") {
				out += " but ";
			}

			out += "not ";

			out += conditions[each]["noroute"][(SizeType)0].GetString();

			for (SizeType j = 1; j < conditions[each]["noroute"].Size(); j++) {
				out += ", ";
				out += conditions[each]["noroute"][j].GetString();
			}
		}

		int Min, Max;
		bool min, max = false;

		if (conditions[each].HasMember("min") && (Min = conditions[each]["min"].GetInt()) > 0) {
			min = true;
		}

		if (conditions[each].HasMember("max") && (Max = conditions[each]["max"].GetInt()) > 0) {
			max = true;
		}

		if (min && max) {
			out += " (FL" + to_string(Min) + " - " + to_string(Max) + ")";
		}
		else if (min) {
			out += " (FL" + to_string(Min) + "+)";
		}
		else if (max) {
			out += " (FL" + to_string(Max) + "-)";
		}
		else {
			out += " (All Levels)";
		}

		outroute.push_back(out);
	}

	string out = "";

	for (string each : outroute) {
		out += each + " / ";
	}

	if (out == "") {
		out = "None";
	}
	else {
		out = out.substr(0, out.length() - 3);
	}

	return "Route. Valid Initial Routes: " + out;
}

string CVFPCPlugin::DestinationOutput(size_t origin_int, size_t pos, vector<size_t> successes) {
	const Value& conditions = config[origin_int]["sids"][pos]["constraints"];
	vector<vector<string>> res{ vector<string>{}, vector<string>{} };

	for (int each : successes) {
		vector<string> good_new_eles{};
		if (conditions[each].HasMember("dests") && conditions[each]["dests"].IsArray() && conditions[each]["dests"].Size()) {
			for (SizeType j = 0; j < conditions[each]["dests"].Size(); j++) {
				string dest = conditions[each]["dests"][j].GetString();

				if (dest.size() < 4)
					dest += string(4 - dest.size(), '*');

				good_new_eles.push_back(dest);
			}
		}

		vector<string> bad_new_eles{};
		if (conditions[each].HasMember("nodests") && conditions[each]["nodests"].IsArray() && conditions[each]["nodests"].Size()) {
			for (SizeType j = 0; j < conditions[each]["nodests"].Size(); j++) {
				string dest = conditions[each]["nodests"][j].GetString();

				if (dest.size() < 4)
					dest += string(4 - dest.size(), '*');

				bad_new_eles.push_back(dest);
			}
		}

		bool added = false;
		for (string dest : res[0]) {
			//Remove Duplicates from Whitelist
			for (size_t k = good_new_eles.size(); k > 0; k--) {
				string new_ele = good_new_eles[k - 1];
				if (new_ele.compare(dest) == 0) {
					good_new_eles.erase(good_new_eles.begin() + k - 1);
				}
			}

			//Prevent Previously Whitelisted Elements from Being Blacklisted
			for (size_t k = bad_new_eles.size(); k > 0; k--) {
				string new_ele = bad_new_eles[k - 1];
				if (new_ele.compare(dest) == 0) {
					bad_new_eles.erase(bad_new_eles.begin() + k - 1);
				}
			}
		}

		//Whitelist Previously Blacklisted Elements
		for (string dest : good_new_eles) {

			for (size_t k = res[1].size(); k > 0; k--) {
				string new_ele = res[1][k - 1];
				if (new_ele.compare(dest) == 0) {
					res[1].erase(res[1].begin() + k - 1);
				}
			}
		}

		//Remove Duplicates from Blacklist
		for (string dest : res[1]) {
			for (size_t k = bad_new_eles.size(); k > 0; k--) {
				string new_ele = bad_new_eles[k - 1];
				if (new_ele.compare(dest) == 0) {
					bad_new_eles.erase(bad_new_eles.begin() + k - 1);
				}
			}
		}

		res[0].insert(res[0].end(), good_new_eles.begin(), good_new_eles.end());
		res[1].insert(res[1].end(), bad_new_eles.begin(), bad_new_eles.end());
	}

	string out = "";

	for (string each : res[0]) {
		out += each + ", ";
	}

	for (string each : res[1]) {
		out += "Not " + each + ", ";
	}

	if (out == "") {
		out = "None";
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Destination. Valid Destinations: " + out;
}

/*string CVFPCPlugin::EngineOutput(size_t origin_int, size_t pos, vector<int> successes) {
	const Value& conditions = config[origin_int]["Sids"][pos]["Constraints"];
	vector<string> engs{};
	for (int each : successes) {
		if (conditions[each].HasMember("Eng") && conditions[each]["Eng"].IsString()) {
			engs.push_back(conditions[each]["Eng"].GetString());
		}
		else if (conditions[each]["Eng"].IsArray() && conditions[each]["Eng"].Size()) {
			for (SizeType j = 0; j < conditions[each]["Eng"].Size(); j++) {
				string eng = conditions[each]["Eng"][j].GetString();

				engs.push_back(eng);
			}
		}
	}

	sort(engs.begin(), engs.end());
	vector<string>::iterator itr = unique(engs.begin(), engs.end());
	engs.erase(itr, engs.end());

	string out = "";

	for (string each : engs) {
		if (each == "P") {
			out += "Piston, ";
		}
		else if (each == "T") {
			out += "Turboprop, ";
		}
		else if (each == "J") {
			out += "Jet, ";
		}
		else if (each == "E") {
			out += "Electric, ";
		}
	}

	if (out == "") {
		out = "None Specified";
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Engine Type. Type Required: " + out;
}*/

/*string CVFPCPlugin::SuffixOutput(size_t origin_int, size_t pos, vector<int> successes) {
	const Value& conditions = config[origin_int]["Sids"][pos]["Constraints"];
	vector<string> suffices{};
	for (int each : successes) {
		if (conditions[each].HasMember("Suf") && conditions[each]["Suf"].IsString()) {
			suffices.push_back(conditions[each]["Suf"].GetString());
		}
	}

	sort(suffices.begin(), suffices.end());
	vector<string>::iterator itr = unique(suffices.begin(), suffices.end());
	suffices.erase(itr, suffices.end());

	string out = "";

	for (string each : suffices) {
		out += each + ", ";
	}

	if (out == "") {
		out = "None Specified";
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Suffix. Valid Suffices: " + out + ".";
}*/

//
void CVFPCPlugin::OnFunctionCall(int FunctionId, const char * ItemString, POINT Pt, RECT Area) {
	if (FunctionId == TAG_FUNC_CHECKFP_MENU) {
		OpenPopupList(Area, "Check FP", 1);
		AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK, false, 2, false);
	}
	if (FunctionId == TAG_FUNC_CHECKFP_CHECK) {
		checkFPDetail();
	}
}

// Get FlightPlan, and therefore get the first waypoint of the flightplan (ie. SID). Check if the (RFL/1000) corresponds to the SID Min FL and report output "OK" or "FPL"
void CVFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize){
	if (validVersion && ItemCode == TAG_ITEM_FPCHECK) {
		string FlightPlanString = FlightPlan.GetFlightPlanData().GetRoute();
		int RFL = FlightPlan.GetFlightPlanData().GetFinalAltitude();

		*pColorCode = TAG_COLOR_RGB_DEFINED;
		string fpType{ FlightPlan.GetFlightPlanData().GetPlanType() };
		if (fpType == "V") {
			*pRGB = TAG_GREEN;
			strcpy_s(sItemString, 16, "VFR");
		}
		else {
			vector<vector<string>> validize = validizeSid(FlightPlan);
			vector<string> messageBuffer{ validize[0] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Syntax, 8 = Passed/Failed

			if (messageBuffer.back() == "Passed") {
				*pRGB = TAG_GREEN;
				strcpy_s(sItemString, 16, "OK!");
			}
			else {
				*pRGB = TAG_RED;
				string code = getFails(validize[0]);
				strcpy_s(sItemString, 16, code.c_str());
			}
		}

	}
}

bool CVFPCPlugin::OnCompileCommand(const char * sCommandLine) {
	//Restart Automatic Data Loading
	if (startsWith(".vfpc load", sCommandLine))
	{
		if (autoLoad) {
			sendMessage("Auto-Load Already Active.");
			debugMessage("Warning", "Auto-load activation attempted whilst already active.");
		}
		else {
			autoLoad = true;
			relCount = 0;
			sendMessage("Auto-Load Activated.");
			debugMessage("Info", "Auto-load reactivated.");
		}
		return true;
	}
	//Activate Debug Logging
	if (startsWith(".vfpc log", sCommandLine)) {
		if (debugMode) {
			debugMessage("Info", "Logging mode deactivated.");
			debugMode = false;
		} else {
			debugMode = true;
			debugMessage("Info", "Logging mode activated.");
		}
		return true;
	}
	//Text-Equivalent of "Show Checks" Button
	if (startsWith(".vfpc check", sCommandLine))
	{
		checkFPDetail();
		return true;
	}
	return false;
}

// Sends to you, which checks were failed and which were passed on the selected aircraft
void CVFPCPlugin::checkFPDetail() {
	if (validVersion) {
		vector<vector<string>> validize = validizeSid(FlightPlanSelectASEL());
		vector<string> messageBuffer{ validize[0] };	// 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = SID Restrictions, 8 = Syntax, 9 = Passed/Failed
		vector<string> logBuffer{ validize[1] };
		sendMessage(messageBuffer.front(), "Checking...");
#
		string buffer{ messageBuffer.at(1) + " | " };
		string logbuf{ logBuffer.at(1) + " | " };

		if (messageBuffer.at(1).find("Invalid") != 0) {
			for (size_t i = 2; i < messageBuffer.size() - 1; i++) {
				string temp = messageBuffer.at(i);
				string logtemp = logBuffer.at(i);

				if (temp != "-") {
					buffer += temp;
					buffer += " | ";
				}

				if (logtemp != "-") {
					logbuf += logtemp;
					logbuf += " | ";
				}
			}
		}

		buffer += messageBuffer.back();
		logbuf += logBuffer.back();

		sendMessage(messageBuffer.front(), buffer);
		debugMessage(logBuffer.front(), logbuf);
	}
}

string CVFPCPlugin::getFails(vector<string> messageBuffer) {
	vector<string> fail;

	if (messageBuffer.at(1).find("Invalid") == 0) {
		fail.push_back("SID");
	}
	if (messageBuffer.at(2).find("Failed") == 0) {
		fail.push_back("DST");
	}
	if (messageBuffer.at(3).find("Failed") == 0) {
		fail.push_back("RTE");
	}
	if (messageBuffer.at(4).find("Failed") == 0) {
		fail.push_back("NAV");
	}
	if (messageBuffer.at(5).find("Failed") == 0) {
		fail.push_back("MIN");
		fail.push_back("MAX");
	}
	if (messageBuffer.at(6).find("Failed") == 0) {
		fail.push_back("DIR");
	}
	if (messageBuffer.at(7).find("Failed") == 0) {
		fail.push_back("RST");
	}
	if (messageBuffer.at(8).find("Invalid") == 0) {
		fail.push_back("CHK");
	}

	return fail[failPos % fail.size()];
}

void CVFPCPlugin::APICalls() {
	validVersion = checkVersion();
	getSids();
	timeCall();
}

void CVFPCPlugin::OnTimer(int Counter) {
	if (validVersion) {
		if (relCount == -1 && fut.valid() && fut.wait_for(1ms) == std::future_status::ready) {
			fut.get();
			relCount = 10;
		}
	
		blink = !blink;

		//2520 is Lowest Common Multiple of Numbers 1-0
		if (failPos < 2520) {
			failPos++;
		}
		//Number shouldn't get out of control
		else {
			failPos = 0;
		}

		if (relCount > 0) {
			relCount--;
		}

		// Loading proper Sids, when logged in
		if (GetConnectionType() != CONNECTION_TYPE_NO && autoLoad && relCount == 0) {
			string callsign{ ControllerMyself().GetCallsign() };
			fut = std::async(std::launch::async, &CVFPCPlugin::APICalls, this);
			relCount--;
		}
	}
}