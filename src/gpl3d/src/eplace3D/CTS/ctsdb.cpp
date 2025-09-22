#include "ctsdb.h"

void CTSDB::initWithPlaceDB(PlaceDB *db)
{
    // pdb=db;
    int sinkCount = db->dbFFs.size();
    dbSinks.resize(sinkCount);

    for (int i = 0; i < sinkCount; i++)
    {
        // make sure index in the vector dbFFs matches the index in the vector dbSinks
        Sink &newSink = dbSinks[i];//? is this effective? need to double check
        newSink.id = i;           // sink id start from 0 //! not equal to FF id!!!!
        newSink.capacitance = 30; // a test value
        newSink.x = db->dbFFs[i]->currentPos.getX(); // use currentPos instead of initialPos
        newSink.y = db->dbFFs[i]->currentPos.getY();
    }
//     // for (Module *curTermial : db->dbTerminals)
//     // {
//     //     Blockage newBlockage;
//     //     newBlockage.ll.x = curTermial->getLL_2D().x;
//     //     newBlockage.ll.y = curTermial->getLL_2D().y;
//     //     newBlockage.ur.x = curTermial->getUR_2D().x;
//     //     newBlockage.ur.y = curTermial->getUR_2D().y;
//     // }
}

void CTSDB::initWithTierFFs(const vector<vector<Module*>>& ffNodesPerTier, 
                           const unordered_map<Module*, ModulePosition>& modulePosition,
                           size_t tierId)
{
    // 只处理指定层的FF节点
    const vector<Module*>& tierFFs = ffNodesPerTier[tierId];
    int sinkCount = tierFFs.size();
    // printf("sink count: %d for tier %zu,   ", sinkCount, tierId);
    dbSinks.resize(sinkCount);

    for (int i = 0; i < sinkCount; i++)
    {
        // 确保dbSinks中的索引与tierFFs中的索引匹配
        Sink &newSink = dbSinks[i];
        newSink.id = i;           // sink id从0开始，注意这与FF的全局ID不同
        newSink.capacitance = 30; // 测试值
        
        // 从modulePosition中获取FF的位置
        Module* ff = tierFFs[i];
        auto posIt = modulePosition.find(ff);
        if (posIt != modulePosition.end()) {
            newSink.x = posIt->second.position.x;
            newSink.y = posIt->second.position.y;
        } else {
            printf("Error: FF %s not found in modulePosition map for tier %zu\n", ff->name.c_str(), tierId);
            exit(EXIT_FAILURE);
        }
    }
    
    // printf("CTSDB initialized for tier %zu with %d FF sinks\n", tierId, sinkCount);
}

void CTSDB::setSinkLocationWithPlaceDB(PlaceDB *db)
{
    int sinkCount = db->dbFFs.size();

    for (int i = 0; i < sinkCount; i++)
    {
        // make sure index in the vector dbFFs matches the index in the vector dbSinks
        Sink &curSink = dbSinks[i];
        assert(curSink.id==i);
        curSink.x = db->dbFFs[i]->currentPos.getX();;
        curSink.y = db->dbFFs[i]->currentPos.getY();;
    }
}

void CTSDB::setSinkLocationWithTierPlacer(const vector<vector<Module*>>& ffNodesPerTier,
                                         const unordered_map<Module*, ModulePosition>& modulePosition,
                                         size_t tierId)
{
    const vector<Module*>& tierFFs = ffNodesPerTier[tierId];
    int sinkCount = tierFFs.size();
    // printf("sink count: %d ,  \n", sinkCount);
    // 确保dbSinks大小正确
    if (dbSinks.size() != sinkCount) {
        printf("Warning: dbSinks size (%zu) doesn't match tier FF count (%d)\n", 
               dbSinks.size(), sinkCount);
        return;
    }

    for (int i = 0; i < sinkCount; i++)
    {
        Sink &curSink = dbSinks[i];
        assert(curSink.id == i);
        
        Module* ff = tierFFs[i];
        auto posIt = modulePosition.find(ff);
        if (posIt != modulePosition.end()) {
            curSink.x = posIt->second.position.x;
            curSink.y = posIt->second.position.y;
        } else {
            printf("Error: FF %s not found in modulePosition map for tier %zu\n", ff->name.c_str(), tierId);
            exit(EXIT_FAILURE);
        }
    }
}

void CTSDB::showCTSdbInfo()
{
    cout.setf(ios::fixed, ios::floatfield);
    cout << padding << " Database summary " << padding << endl;
    cout << "Sink count: " << dbSinks.size() << endl;
    cout << "Blockage count: " << dbBlockages.size() << endl;

    // for(Sink sink:dbSinks)
    // {
    //     cout<<"sink: "<<sink.x<<" "<<sink.y<<" "<<sink.capacitance<<endl;
    // }
    // cout<<"========================"<<endl;
    // for(Blockage blockage:dbBlockages)
    // {
    //     cout<<blockage.ll.x<<" "<<blockage.ll.y<<" "<<blockage.ur.x<<" "<<blockage.ur.y<<endl;
    // }
}