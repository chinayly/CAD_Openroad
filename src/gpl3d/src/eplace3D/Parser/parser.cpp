#include "parser.h"
#include "arghandler.h"

int BookshelfParser::ReadFile(string file, PlaceDB &db)
{
	cout << "Reading AUX file: " << file << endl;

	ifstream out(file);
	if (!out)
	{
		cout << "\tCannot open aux file\n";
		exit(-1);
	}

	char tmp[10000];
	out.getline(tmp, 10000);

	char tmp1[500];
	char tmp2[500];
	char file_nodes[500];
	char file_nets[500];
	char file_wts[500];
	char file_pl[500];
	char file_scl[500];
	char file_tech[500];
	char file_ffs[500];

	sscanf(tmp, "%s %s %s %s %s %s %s %s %s",
		   tmp1, tmp2, file_nodes, file_nets, file_wts, file_pl, file_scl, file_tech, file_ffs);

	printf("\t\t%s %s %s %s %s %s %s\n", file_nodes, file_nets, file_wts, file_pl, file_scl, file_tech, file_ffs);

	// Read SCL first to know the row height.
	ReadSCLFile(file_scl, db);	   // read core-row information
	ReadNodesFile(file_nodes, db); // blocks & cell width/height
	ReadNetsFile(file_nets, db);   // read net file
	ReadPLFile(file_pl, db, true); // initial module locations
	ReadFFsFile(file_ffs, db);   // read flipflop file
	return 0;
}

int BookshelfParser::ReadSCLFile(string file, PlaceDB &db)
{
	// In this version, we don't care the pre-placed location.
	// Read Numrows, Height, and Numsites only.

	string path;
	gArg.GetString("path", &path);
	path += file;
	ifstream in(path.c_str());
	if (!in)
	{
		cerr << "Cannot open .scl file: " << file << endl;
		exit(-1);
	}

	int nRows, height;
	nRows = height = -1;

	int lineNumber = 0;

	// check file format string
	char tmp[10000];
	in.getline(tmp, 10000);
	lineNumber++;

#if 0
	if( strcmp( "UCLA scl 1.0", tmp ) != 0 )
	{
		cerr << "SCL file header error (UCLA scl 1.0)\n";
		exit(-1);
	}
#endif

	char tmp1[500], tmp2[500], tmp3[500], tmp4[500], tmp5[500], tmp6[500];

	vector<SiteRow> &vSites = db.dbSiteRows;
	double SiteWidth = 1; // default value is 1 (2005/2/14 donnie)
	ORIENT orient = OR_N; // 2006-04-23 (donnie)
	while (in.getline(tmp, 10000))
	{
		tmp1[0] = tmp2[0] = tmp3[0] = tmp4[0] = tmp5[0] = tmp6[0] = '\0';
		sscanf(tmp, "%s %s %s %s %s %s", tmp1, tmp2, tmp3, tmp4, tmp5, tmp6);

		if (strcmp(tmp1, "Numrows") == 0 || strcmp(tmp1, "NumRows") == 0)
		{
			nRows = atoi(tmp3);
			////test code
			// printf("get numrows %f\n", atoi( tmp3 ) );
			////@test code
		}
		else if (strcmp(tmp1, "CoreRow") == 0 && strcmp(tmp2, "Horizontal") == 0) // start of a row
		{
			vSites.push_back(SiteRow());
			////test code
			// printf("get corerow, site size: %d\n", vSites.size() );
			////@test code
		}
		else if (strcmp(tmp1, "Coordinate") == 0)
		{
			vSites.back().bottom = atof(tmp3);
			////test code
			// printf("get coordinate %f\n", atof( tmp3 ) );
			////@test code
		}
		else if (strcmp(tmp1, "Height") == 0)
		{
			vSites.back().height = atof(tmp3);
			height = atof(tmp3);
			////test code
			// printf("get height %f\n", atof( tmp3 ) );
			////@test code
		}
		else if (strcmp(tmp1, "Sitewidth") == 0) //! site spacing equals to site width in bookshelf format
		{
			SiteWidth = atof(tmp3);
			vSites.back().step = atof(tmp3); // step equals sitewidth in bookshelf format
											 ////test code
											 // printf("get sitewidth %f\n", atof( tmp3 ) );
											 ////@test code
		}
		else if (strcmp(tmp4, "Numsites") == 0 || strcmp(tmp4, "NumSites") == 0)
		{
			double subOrigin = atof(tmp3); //! subrowOrigin
			double numSites = atof(tmp6);
			// initialize interval
			vSites.back().intervals.push_back(Interval(subOrigin, (numSites * SiteWidth) + subOrigin));

			vSites.back().start = POS_2D(subOrigin, vSites.back().bottom);
			vSites.back().end = POS_2D((numSites * SiteWidth) + subOrigin, vSites.back().bottom);
			// printf("get numsites: %f %f\n", atof(tmp3), ( atof( tmp6 )*SiteWidth ) + atof( tmp3 ) );
		}
		else if (strcmp(tmp1, "Siteorient") == 0 || strcmp(tmp1, "SiteOrient") == 0) // donnie 2006-04-23
		{
			if (strcmp(tmp3, "S") == 0 || strcmp(tmp3, "FS") == 0)
				orient = OR_S;
			else
				orient = OR_N;
			vSites.back().orientation = orient;
		}
	}

	// cout << "     Numrows: " << nRows << "\n";
	// cout << "      Height: " << height << "\n";
	// cout << "    Numsites: " << nSites << "\n";
	// cout << " Core region: (" << left << "," << bottom << ")-("
	//	 << left+nSites << "," << bottom + nRows * height << ")\n";

	db.commonRowHeight = vSites.back().height; //!
	// Romove the sites occupied by the fixed module

	// included in CPlaceDB
	//@Romove the sites occupied by the fixed module

	return 0;
}

int BookshelfParser::ReadNodesFile(string file, PlaceDB &db)
{
	string path;
	gArg.GetString("path", &path);
	path += file;
	ifstream in(path.c_str());
	if (!in)
	{
		cerr << "\tCannot open nodes file: " << file << endl;
		exit(-1);
	}

	int nModules, nNodes, nTerminals;
	nModules = nNodes = nTerminals = -1;

	int lineNumber = 0;

	// check file format string
	char tmp[10000], tmp2[10000], tmp3[10000];
	in.getline(tmp, 10000);
	lineNumber++;

#if 0
	if( strcmp( "UCLA nodes 1.0", tmp ) != 0 )
	{
		cerr << "Nodes file header error (not UCLA nodes 1.0)\n";
		return -1;
	}
#endif

	// check file header
	int checkFormat = 0;
	while (in.getline(tmp, 10000))
	{
		lineNumber++;

		// cout << tmp << endl;
		if (tmp[0] == '#')
			continue;
		if (strncmp("NumNodes", tmp, 8) == 0)
		{
			char *pNumber = strrchr(tmp, ':');
			nModules = atoi(pNumber + 1);
			checkFormat++;
		}
		else if (strncmp("NumTerminals", tmp, 12) == 0)
		{
			char *pNumber = strrchr(tmp, ':');
			nTerminals = atoi(pNumber + 1);
			checkFormat++;
		}

		if (checkFormat == 2)
			break;
	}
	nNodes = nModules - nTerminals;

	if (checkFormat != 2)
	{
		cerr << "** Block file header error (miss NumNodes or NumTerminals)\n";
	}
	cout << "    NumModules: " << nModules << endl;
	cout << "    NumNodes: " << nNodes;
	if (nNodes > 1000)

		cout << " (= " << nNodes / 1000 << "k)";
	cout << endl;
	cout << "    Terminals: " << nTerminals << endl;

	// db.allocateModuleMemory(nModules);

	// Read modules and terminals.
	char name[10000];
	char type[10000];
	double width, height;
	int nodeIndex = 0;
	int terminalIndex = 0;
	Module *curModule = NULL;
	while (in.getline(tmp, 10000))
	{

		lineNumber++;

		if (tmp[0] == '\0')
			continue;
		type[0] = '\0';
		sscanf(tmp, "%s %s %s %s",
			   name, tmp2, tmp3, type);

		width = atof(tmp2);
		height = atof(tmp3);

#if 0
        if( oldH != -1 && h != oldH && strcmp( type, "terminal" ) != 0)
        {
            cerr << "The program cannot handle mixed-size benchmark currently.";
            exit(0);
        }
        oldH = h;
#endif

		if (strcmp(type, "terminal") == 0)
		{
			db.addModule(name, width, height, FIXED , false, false, true); 
			terminalIndex++;
		}
		else if (strcmp(type, "terminal_NI") == 0) // (frank) 2022-05-13 consider terminal_NI
		{
			db.addModule(name, width, height, FIXED, true, false, true); 
			terminalIndex++;
		}
		else
		{
			db.addModule(name, width, height, FREE, false, false, false); 
			nodeIndex++;
		}
	}

	// check if modules number and terminal number match
	if (terminalIndex + nodeIndex != nModules)
	{
		cerr << "Error: There are " << terminalIndex + nodeIndex << " modules in the file\n";
		exit(-1);
	}
	if (terminalIndex != nTerminals)
	{
		cerr << "Error: There are " << terminalIndex << " terminals in the file\n";
		exit(-1);
	}
	return 0;
}

int BookshelfParser::ReadNetsFile(string file, PlaceDB &db)
{
	string path;
	gArg.GetString("path", &path);
	path += file;
	ifstream in(path.c_str());
	if (!in)
	{
		cerr << "Cannot open net file: " << file << endl;
		exit(-1);
	}

	int nNets, nPins;
	nNets = nPins = -1;

	int lineNumber = 0;

	// check file format string
	char tmp[10000];
	in.getline(tmp, 10000);
	lineNumber++;

#if 0
	if( strcmp( "UCLA nets 1.0", tmp ) != 0 )
	{
		cerr << "Nets file header error (UCLA nets 1.0)\n";
		exit(-1);
	}
#endif

	// check file header
	int checkFormat = 0;
	while (in.getline(tmp, 10000))
	{
		lineNumber++;

		// cout << tmp << endl;
		if (tmp[0] == '#')
			continue;
		if (strncmp("NumNets", tmp, 7) == 0)
		{
			char *pNumber = strrchr(tmp, ':');
			nNets = atoi(pNumber + 1);
			db.allocateNetMemory(nNets);
			checkFormat++;
		}
		else if (strncmp("NumPins", tmp, 7) == 0)
		{
			char *pNumber = strrchr(tmp, ':');
			nPins = atoi(pNumber + 1);
			db.allocatePinMemory(nPins);
			checkFormat++;
		}

		if (checkFormat == 2)
			break;
	}

	if (checkFormat != 2)
	{
		cerr << "** Net file header error\n";
	}

	cout << "         Nets: " << nNets << endl;
	cout << "         Pins: " << nPins << endl;

	char tmp1[2000], tmp2[2000], tmp3[2000], tmp4[2000];
	int maxDegree = 0;
	int degree;
	int pinIndex = 0;
	int netIndex = 0;
	while (in.getline(tmp, 10000))
	{
		lineNumber++;

		if (tmp[0] == '\0')
			continue;

		sscanf(tmp, "%s : %d   %s", tmp1, &degree, tmp2);
		if (strcmp(tmp1, "NetDegree") != 0 || degree < 0)
		{
			cerr << "Syntax unsupport in line " << lineNumber << ": "
				 << tmp2 << endl;
			return 01;
		}

		int vCount;
		double xOffset, yOffset;
		if (degree > maxDegree)
			maxDegree = degree;
		vector<Pin*> curNetPins;
		string netName(tmp2);
		curNetPins.clear();
		pinIndex += degree; // will read "degree" pins

		for (int j = 0; j < degree; j++)
		{
			PinDirection pinDirection;
			in.getline(tmp, 10000);
			lineNumber++;
			tmp3[0] = '\0';
			tmp4[0] = '\0';
			vCount = sscanf(tmp, "%s %s : %s %s", tmp1, tmp2, tmp3, tmp4);

			if (tmp3[0] != '\0')
				xOffset = atof(tmp3);
			else
				xOffset = 0;
			if (tmp4[0] != '\0')
				yOffset = atof(tmp4);
			else
				yOffset = 0;
			if (tmp2[0] == 'I')
			{
				pinDirection = PIN_DIRECTION_IN;
			}
			else if (tmp2[0] == 'O')
			{
				pinDirection = PIN_DIRECTION_OUT;
			}
			else
			{
				pinDirection = PIN_DIRECTION_UNDEFINED;
			}

			Module* module = db.getModuleFromName(tmp1);
			Pin* pin = db.addPin(module, xOffset, yOffset,pinDirection);
			curNetPins.push_back(pin);
			module->addPin(pin);

			// 2005/2/2 (donnie)
			// TODO: Remove duplicate netsIds
			// 2007/3/9 (indark)
			// remove duplicated netsIds
			//? is this necessary?
			// bool found = false;
			// for (unsigned int z = 0; z < db.m_modules[moduleId].m_netsId.size(); z++)
			// {
			// 	if (nReadNets == db.m_modules[moduleId].m_netsId[z])
			// 	{
			// 		found = true;
			// 		break;
			// 	}
			// }
			// if (!found)
			// 	db.m_modules[moduleId].m_netsId.push_back(nReadNets);
		}
		db.addNet(netName,curNetPins);
		netIndex++;

		// if (nReadNets % stepNet == 0 && nNets > stepNet)
		// 	printf("#%d...\n", nReadNets);
	}
	db.maxNetDegree = maxDegree;

	// TODO: if nReadNets > nNets may have memory problem.
	// check if modules number and terminal number match
	if (nNets != netIndex)
	{
		cerr << "Error: There are " << netIndex << " nets in the file\n";
		exit(-1);
	}
	if (pinIndex != nPins)
	{
		cerr << "Error: There are " << pinIndex << " pins in the file\n";
		exit(-1);
	}

#if 1
	cout << "Max net degree= " << maxDegree << endl;
#endif
	return 0;
}

int BookshelfParser::ReadPLFile(string file, PlaceDB &db, bool init)
{
	string path;
	if (init)
	{
		gArg.GetString("path", &path);
		path += file;
		cout << "Initialize module position with file: " << file << "\n";
	}
	else
	{
		path = file;
		cout << "Setting module position with file: " << file << "\n";
	}

	// printf("module numbers = %zu\n", db.dbModules.size());

	ifstream in(path.c_str());
	if (!in)
	{
		cerr << "\tCannot open PL file after lgdp: " << file << endl;
		exit(-1);
	}

	int lineNumber = 0;

	// check file format string
	char tmp[10000];
	in.getline(tmp, 10000);
	lineNumber++;
	// if( strcmp( "UCLA pl 1.0", tmp ) != 0 )
	//{
	//	cerr << "PL file header format error (UCLA pl 1.0)\n";
	//	return -1;
	// }

	char name[10000];
	char orientation[1000];
	float x, y;
	while (in.getline(tmp, 10000))
	{
		lineNumber++;

		// cout << tmp << endl;
		if (tmp[0] == '#')
			continue;
		if (tmp[0] == '\0')
			continue;

		for (int i = 0; i < (int)strlen(tmp); i++)
		{
			if (tmp[i] == '(')
				tmp[i] = ' ';
			if (tmp[i] == ',')
				tmp[i] = ' ';
			if (tmp[i] == ')')
				tmp[i] = ' ';
			if (tmp[i] == ':')
				tmp[i] = ' ';
			if (tmp[i] == '=')
				tmp[i] = ' ';
			if (tmp[i] == '\r')
				tmp[i] = ' ';
		}

		name[0] = '\0';
		int ret = sscanf(tmp, "%s %f %f %s ", name, &x, &y, orientation);

		if (ret <= 0)
			continue;

		if (ret != 4 && ret != 3)
		{
			// cerr << "Error in the PL file: <" << tmp << ">\n";
			// exit(-1);
			printf("Syntax (may) error in line %d. Please check. (ret = %d)\n",
				   lineNumber, ret);
			continue; // skip this line...
		}

		if (ret == 3)
		{
			// printf( "Block %s does not has orientation.\n", name );
			orientation[0] = 'N';
			orientation[1] = '\0';
		}

		Module *module = db.getModuleFromName(name);
		// assert(module);
		if (!module)
		{
			if (strstr(name, "TSV") != NULL)  //! should be  fixed later
			{
				// Skip TSV modules as they may not be defined in nodes file
				continue;
			}
			cerr << "Error: module name " << name << " not found in line "
				 << lineNumber << " file: " << file << endl;
			exit(-1);
		}
		if (init)
		{
			module->setInitialPosition(ModulePosition(0,x,y));
			module->currentPos = module->initialPos; // set current position to initial position
		}
		else
		{
			module->currentPos.position.x = x;
			module->currentPos.position.y = y;
		}

	}
	return 0;
}

int BookshelfParser::ReadFFsFile(string file, PlaceDB &db)
{
	cout << "Initialize ffs with file: " << file << "\n";
	string path;
	gArg.GetString("path", &path);
	path += file;
	ifstream in(path.c_str());
	if (!in)
	{
		cerr << "Cannot open .ffs file: " << file << endl;
		exit(-1);
	}
	char tmp[10000];
	in.getline(tmp, 10000);

	int ffCount;

	char tmp1[500], tmp2[500], tmp3[500];
	char name[500];
	vector<SiteRow> &vSites = db.dbSiteRows;
	double SiteWidth = 1; // default value is 1 (2005/2/14 donnie)
	ORIENT orient = OR_N; // 2006-04-23 (donnie)
	while (in.getline(tmp, 10000))
	{
		if (tmp[0] == '#')
			continue;
		if (tmp[0] == '\0')
			continue;
		tmp1[0] = tmp2[0] = tmp3[0] = '\0';
		sscanf(tmp, "%s %s %s", tmp1, tmp2, tmp3);

		if (strcmp(tmp1, "NumFFs") == 0 || strcmp(tmp1, "Numffs") == 0)
		{
			ffCount = atoi(tmp3);
			////test code
			// printf("get ffcount: %s\n", tmp3);
			cout << "ff count: " << ffCount << endl;
			////@test code
			break;
		}
	}

	for (int i = 0; i < ffCount; i++)
	{
		if (tmp[0] == '\0')
			continue;
		in.getline(tmp, 10000);
		name[0] = '\0';
		sscanf(tmp, "%s", name);
		Module *module = db.getModuleFromName(name);
		assert(module);
		module->isFF = true;
		db.dbFFs.push_back(module);
		// cout << name << endl;
	}

	return 0;
}