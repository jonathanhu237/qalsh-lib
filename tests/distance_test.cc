#include <qalsh/qalsh.h>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

void Check(bool b) { if (!b) throw std::runtime_error("distance regression failed"); }
void CheckProjectionGrouping() {
    std::mt19937 generator(7);
    std::uniform_real_distribution<float> random(-1,1);
    for (unsigned dimension : {1U,3U,4U,5U,63U,64U,65U,300U,784U,960U}) {
        std::vector<std::vector<float>> points(12,std::vector<float>(dimension));
        for(auto& p:points) for(auto& x:p) x=random(generator);
        qalsh::PointAccessor accessor=[&](qalsh::PointId id)->qalsh::PointView{return points.at(id);};
        qalsh::IndexConfig config;
        config.num_points=12;config.num_dimensions=dimension;
        config.qalsh.num_hash_tables=5;config.qalsh.collision_threshold=1;config.qalsh.bucket_width=2;
        config.projection_vectors.resize(5*dimension);
        for(auto& x:config.projection_vectors)x=random(generator);
        const auto grouped=qalsh::InMemoryIndex::Build(config,accessor);
        auto read=[](const qalsh::ProjectionIndex& index,unsigned table){
            std::vector<float> values(12);
            auto cursor=index.make_cursor(table,0);
            const auto report=index.scan(*cursor,std::numeric_limits<float>::max(),100,&values,
                [](void* p,const qalsh::ProjectionHit& h){(*static_cast<std::vector<float>*>(p)).at(h.point_id)=h.projected_value;});
            Check(report.table_exhausted);
            return values;
        };
        for(unsigned table=0;table<5;++table){
            auto single=config;single.qalsh.num_hash_tables=1;
            single.projection_vectors.assign(config.projection_vectors.begin()+table*dimension,
                                               config.projection_vectors.begin()+(table+1)*dimension);
            const auto separate=qalsh::InMemoryIndex::Build(single,accessor);
            Check(read(*grouped,table)==read(*separate,0));
        }
    }
}
int main() {
    CheckProjectionGrouping();
    std::mt19937 generator(42);
    std::uniform_real_distribution<float> distribution(-1,1);
    for (unsigned dimension : {1U,2U,63U,64U,65U,784U,4096U}) {
        std::vector<float> a(dimension),b(dimension);
        for (auto& x:a) x=distribution(generator);
        for (auto& x:b) x=distribution(generator);
        for (auto norm : {qalsh::Metric::l1,qalsh::Metric::l2}) {
            const float d=qalsh::DistanceValue(a,b,norm);
            const auto unbounded=qalsh::BoundedDistanceValue(a,b,norm,std::numeric_limits<float>::infinity());
            const auto bounded=qalsh::BoundedDistanceValue(a,b,norm,d);
            Check(unbounded.exact && bounded.exact && d==unbounded.distance && d==bounded.distance);
            const auto too_small=qalsh::BoundedDistanceValue(a,b,norm,std::nextafter(d,0.0F));
            Check(!too_small.exact && std::isinf(too_small.distance));
        }
    }
    const float zero=0;
    for (float value : {1e-30F,1e-22F,1e20F}) {
        const qalsh::PointView a(&value,1),b(&zero,1);
        Check(qalsh::L2Distance(a,b)==value);
        const auto bounded=qalsh::BoundedDistanceValue(a,b,qalsh::Metric::l2,value);
        Check(bounded.exact && bounded.distance==value);
    }
    // width*radius may overflow float even when width*radius/2 fits.
    const float far=2e38F;
    qalsh::IndexConfig config;config.num_points=1;config.num_dimensions=1;
    config.qalsh.num_hash_tables=1;config.qalsh.collision_threshold=1;config.qalsh.bucket_width=2.5F;
    config.projection_vectors={1};
    qalsh::PointAccessor accessor=[&](qalsh::PointId)->qalsh::PointView{return {&far,1};};
    qalsh::SearchEngine engine(qalsh::InMemoryIndex::Build(config,accessor),accessor);
    qalsh::DefaultQalshStrategy strategy;
    const auto result=engine.search({&zero,1},1,strategy);
    Check(result.complete && result.neighbors.size()==1 && result.neighbors[0].distance==far);

    const float maximum=std::numeric_limits<float>::max();
    const std::vector<float> a{maximum,maximum},b{0,0};
    for (auto norm : {qalsh::Metric::l1,qalsh::Metric::l2}) {
        bool threw=false;
        try { (void)qalsh::DistanceValue(a,b,norm); }
        catch(const std::overflow_error&) { threw=true; }
        Check(threw);
        Check(!qalsh::BoundedDistanceValue(a,b,norm,maximum).exact);
    }
}
