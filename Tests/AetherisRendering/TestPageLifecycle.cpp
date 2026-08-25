#include "navigation.hpp"
#include <cassert>
#include <iostream>
using namespace aetheris::rendering;
int main(){
 ResourceCache cache;
 auto fetch=[&](Url const& u)->std::optional<ResourceData>{
   if(u.serialized()=="https://example.test/index.html") return ResourceData{ResourceType::Document,u.serialized(),std::vector<unsigned char>{'<','l','i','n','k',' ','r','e','l','=','\"','s','t','y','l','e','s','h','e','e','t','\"',' ','h','r','e','f','=','\"','a','.','c','s','s','\"','>','<','l','i','n','k',' ','r','e','l','=','\"','s','t','y','l','e','s','h','e','e','t','\"',' ','h','r','e','f','=','\"','b','.','c','s','s','\"','>','<','i','m','g',' ','s','r','c','=','\"','p','.','p','p','m','\"','>'}};
   if(u.serialized()=="https://example.test/a.css"||u.serialized()=="https://example.test/b.css") return ResourceData{ResourceType::Stylesheet,u.serialized(),std::vector<unsigned char>{'i','m','g','{','w','i','d','t','h',':','1','p','x',';','}'}};
   if(u.serialized()=="https://example.test/p.ppm") return ResourceData{ResourceType::Image,u.serialized(),std::vector<unsigned char>{'P','3','\n','1',' ','1','\n','2','5','5','\n','0',' ','0',' ','0','\n'}};
   return std::nullopt;};
 ResourceLoader loader(cache,fetch); NavigationController nav; auto r=nav.navigate({Url::parse("https://example.test/index.html"),{},640},loader); assert(r.succeeded()); assert(nav.lifecycle().state==PageLoadState::Complete); assert(cache.contains("https://example.test/a.css")); assert(cache.contains("https://example.test/b.css")); assert(cache.contains("https://example.test/p.ppm")); r=nav.reload(loader); assert(r.succeeded()); std::cout<<"Page lifecycle tests passed\n";
}
