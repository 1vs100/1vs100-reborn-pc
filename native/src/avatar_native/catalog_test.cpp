#include "native_bridge.h"
#include <iostream>
#include <stdexcept>
namespace a=xe::kernel::xna::avatar;
namespace xe::kernel { thread_local uint8_t* native_avatar_base=nullptr; }
static a::Catalog* catalog=nullptr;
namespace xe::kernel::xna { avatar::Catalog* XnaAvatarCatalog() { return catalog; } }
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int main(int argc,char** argv) {
 try {
  require(argc==2,"Specify the existing content root");
  catalog=a::SharedCatalog(std::filesystem::path(argv[1]));
  require(catalog!=nullptr,"Real Avatar pack failed to load");
  std::vector<uint8_t> memory(0x300000);
  xe::kernel::native_avatar_base=memory.data();
  std::mt19937 generator(12345);
  unsigned models=0,cases=0;
  for(unsigned coordinate=0;coordinate<2;coordinate++) {
   xe::kernel::xam::SetAvatarCoordinateSystem(coordinate);
   for(int body=0;body<2;body++) {
    auto description=a::RandomDescription(*catalog,generator,body);
    const auto manifest=a::SerializeManifest(catalog,description,1);
    require(xe::kernel::xam::BuildAvatarAssets(manifest.data(),manifest.size(),4095,0x1000,0x20000)==0,"Asset writer rejected catalog avatar");
    auto read=[&](uint32_t address) { require(uint64_t(address)+4<=memory.size(),"Pointer outside test memory");return xe::load_and_swap<uint32_t>(memory.data()+address); };
    const auto skeleton=read(0x1000),count=read(skeleton),joints=read(skeleton+4);
    require(count==71||count==72,"Unexpected original skeleton size");
    require(read(joints)==255,"Root must have console missing-parent sentinel255");
    for(unsigned j=0;j<count;j++) {
     const auto parent=read(joints+j*96);
     require(parent==255||parent<j,"Parent must be absent or computed before child");
     for(unsigned link=1;link<3;link++){const auto value=read(joints+j*96+link*4);require(value==255||value<count,"Invalid child/sibling link");}
    }
    const auto n=read(0x1008),records=read(0x1010);
    require(n>0&&n<32,"No real component models generated");
    require(records>=0x1000&&uint64_t(records)+n*52<=0x11000,"Model descriptors outside CPU arena");
    models+=n;cases++;
   }
  }
  std::cout<<"{\"catalog_loaded\":true,\"cases\":"<<cases<<",\"real_model_components\":"<<models<<",\"male_female_and_both_coordinates\":true,\"joint_links_valid\":true,\"runtime_rendering_verified\":false}\n";
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
