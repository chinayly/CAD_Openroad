#include "placer.h"

TimingDrivenPartitioner::TimingDrivenPartitioner(PlaceDB& db): db(db){
    partition_result.reserve(db.dbNodes.size());
}
