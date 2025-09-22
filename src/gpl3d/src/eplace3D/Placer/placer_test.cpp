#include "placer.h"
#include "placedb.h"
#include "parser.h"
// 添加CTS相关头文件
#include "ctsdb.h"
#include "mmm.h"
#include "topology.h"
#include "DME.h"


int main(int argc, char *argv[])
{
    printf("Command line arguments:\n");
    for (int i = 0; i < argc; ++i) {
        printf("argv[%d]: %s\n", i, argv[i]);
    }
    printf("=========================================\n");
    setbuf(stdout, nullptr);
    PlaceDB db;
    gArg.Init(argc, argv);
    if (argc < 2)
    {
        return 0;
    }
    if (strcmp(argv[1] + 1, "aux") == 0) // -aux, argv[1]=='-'
    {
        // bookshelf
        printf("Use BOOKSHELF placement format\n");
        string benchmarkName;
        string filename = argv[2];
        string::size_type pos = filename.rfind("/");
        if (pos != string::npos)
        {
            printf("    Path = %s\n", filename.substr(0, pos + 1).c_str());
            gArg.Override("path", filename.substr(0, pos + 1));

            int length = filename.length();

            benchmarkName = filename.substr(pos + 1, length - pos);

            int len = benchmarkName.length();
            if (benchmarkName.substr(len - 4, 4) == ".aux")
            {
                benchmarkName = benchmarkName.erase(len - 4, 4);
            }
            gArg.Override("benchmarkName", benchmarkName);
            printf("    Benchmark: %s\n", benchmarkName.c_str()); 
        }

        BookshelfParser parser;
        parser.ReadFile(argv[2], db);
    }

    string outputFilePath = "./temp/";
    if (gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir", &outputFilePath);
        outputFilePath += "forLGDP/";
        gArg.Override("BookshelfoutputDir", outputFilePath);
    }

    double GPtime;

    time_start(&GPtime);

    int tierCount = 2;
    if (gArg.CheckExist("t3"))
    {
        tierCount = 3;
    }
    if (gArg.CheckExist("t4"))
    {
        tierCount = 4;
    }



    if (gArg.CheckExist("pseudoClkNet"))
    {
        printf("Adding clock net gradient in space placer\n");
    }
    if (gArg.CheckExist("TPpseudoClkNet"))
    {
        printf("Adding clock net gradient in tier placer\n");
    }
    if (gArg.CheckExist("pseudoClkMassCenter"))
    {
        printf("Adding clock mass center gradient in space placer\n");
    }
    if (gArg.CheckExist("clkwirelevel"))
    {
        printf("Adding clock wirelength level weight\n");
    }

    
    db.convertTo3d(tierCount);
    SpacePlacer placer(&db, 0.95);
    placer.placeInitialization();
    placer.place();

    printf("\n====Space Placement finished, now start partition and tier placement====\n");
    std::unordered_map<Module *, ModulePosition> partitionedPosition;
    partitionedPosition = placer.getPartitionedPosition();

    TierPlacer tp(&db, std::move(partitionedPosition), 0.95);
    tp.place();

    time_end(&GPtime);

    printf("\nGlobal Placement time: %.2f seconds\n\n", GPtime);



    ///////////////////////////////////////////////////
    // legalization and detailed placement
    ///////////////////////////////////////////////////

    // printf("===start legalization and detailed placement===\n");
    // double LGtotalClockWL = 0.0;
    // double DPtotalClockWL = 0.0;
    // double ClkDPtotalClockWL = 0.0;
    // for (size_t tierId = 0; tierId < tierCount; tierId++)
    // {
    //     string legalizerPath;

    //     legalizerPath = "../../3DPlacer/supportbin";

    //     string outputAUXPath;
    //     string outputPLPath;
    //     string outputPath;
    //     string benchmarkName;

        
    //     gArg.GetString("benchmarkName", &benchmarkName);
    //     gArg.GetString("BookshelfoutputDir", &outputAUXPath);
    //     outputAUXPath += benchmarkName;
    //     outputAUXPath += "-lgdp_tier" + to_string(tierId) + ".aux";


    //     gArg.GetString("BookshelfoutputDir", &outputPLPath);
    //     outputPLPath += benchmarkName;
    //     outputPLPath += "-lgdp_tier" + to_string(tierId) + ".pl";

    //     gArg.GetString("BookshelfoutputDir", &outputPath);
    //     // outputPath += benchmarkName;
    //     // outputPath += "-tier" + to_string(tierId)
        
    //     // Check if results file exists and remove it
    //     string resultsFile = outputPath + "Results" + to_string(tierId) + ".txt";
    //     if (access(resultsFile.c_str(), F_OK) == 0) {
    //         remove(resultsFile.c_str());
    //         printf("Removed existing results file: %s\n", resultsFile.c_str());
    //     }
    //     string cmd = legalizerPath + "/ntuplace3" + " -aux " + outputAUXPath + " -loadpl " + outputPLPath + " -noglobal" +  
    //                             " -out " + outputPath  + benchmarkName + "-lgdp_tier" + to_string(tierId) + " > " + outputPath + "Results" +to_string(tierId) + ".txt";
    //     cout << RED << "Running legalizer: " << cmd << RESET << endl;
    //     system(cmd.c_str());


    //     // PlaceDB dpdb;
    //     PlaceDB tierdb;
    //     BookshelfParser tierparser;

    //     string tierauxFile;
    //     tierauxFile = outputPath + benchmarkName + "-lgdp_tier" + to_string(tierId) + ".aux";
    //     printf("AUX: %s\n", tierauxFile.c_str());
    //     gArg.Override("path", outputPath);
    //     tierparser.ReadFile(tierauxFile, tierdb);
        
    //     // After LG CNS

    //     tierparser.ReadPLFile(outputPath  + benchmarkName + + "-lgdp_tier" + to_string(tierId) + ".lg.pl", tierdb, false);
        

    //     CTSDB *ctsdb = new CTSDB();
    //     ctsdb->initWithPlaceDB(&tierdb);
    //     TreeTopology *topo = new TreeTopology(ctsdb);
    //     ZSTDMERouter *router = new ZSTDMERouter(ctsdb);
    //     router->setDelayModel(ELMORE_DELAY); // 使用Elmore延迟模型
    //     router->setTopology(topo);
    //     router->ctsdb->setSinkLocationWithPlaceDB(&tierdb);
    //     topo->clearTopology();
    //     topo->buildTreeUsingNearestNeighborGraph_BucketDecomposition();
    //     router->topDown();
    //     // cout << "\nbefore legalization: \n";
    //     double lgclkWL = router->buildSolution();
    //     LGtotalClockWL += lgclkWL;
    //     printf("LG Clock wirelength for tier %zu: %.2f\n", tierId, lgclkWL);

    //     // After DP CNS
    //     tierparser.ReadPLFile(outputPath  + benchmarkName + + "-lgdp_tier" + to_string(tierId) + ".ntup.pl", tierdb, false);
        
    //     ctsdb->initWithPlaceDB(&tierdb);
    //     TreeTopology *dptopo = new TreeTopology(ctsdb);
    //     ZSTDMERouter *dprouter = new ZSTDMERouter(ctsdb);
    //     dprouter->setDelayModel(ELMORE_DELAY); // 使用Elmore延迟模型
    //     dprouter->setTopology(dptopo);
    //     dprouter->ctsdb->setSinkLocationWithPlaceDB(&tierdb);
    //     dptopo->clearTopology();
    //     dptopo->buildTreeUsingNearestNeighborGraph_BucketDecomposition();
    //     dprouter->topDown();
    //     // cout << "\nbefore legalization: \n";
    //     double dpclkWL = dprouter->buildSolution();
    //     DPtotalClockWL += dpclkWL;
    //     printf("DP Clock wirelength for tier %zu: %.2f\n", tierId, dpclkWL);


    //     //clock aware-DP from ziang's work
    //     string outputLGPLPath;
    //     gArg.GetString("BookshelfoutputDir", &outputLGPLPath);
    //     outputLGPLPath += benchmarkName;
    //     outputLGPLPath += "-lgdp_tier" + to_string(tierId) + ".lg.pl";

    //     string cmddp = legalizerPath + "/ctsdp" + " -aux " + outputAUXPath + " -loadpl " + outputLGPLPath +   
    //                             " -outputPath " + outputPath  + 
    //                             " -clockAware " + " -clockNetWeight 0.6 " + " -averageFFCount 20 " +
    //                             " -> " + outputPath + "clkdp" + to_string(tierId) + ".log" 
    //                             ;
    //     cout << RED << "Running clk-dp: " << cmddp << RESET << endl;
    //     system(cmddp.c_str());

    
    // }

    // printf("\nTotal clock wirelength sum of all tiers: \n");
    // printf("After Legalization:  %.2f\n", LGtotalClockWL);
    // printf("After Detail Place:  %.2f\n", DPtotalClockWL);
    

}