#include "browser_chrome.hpp"
namespace aetheris::rendering {
BrowserChrome::BrowserChrome(BrowserApplication& app):m_application(app){}
bool BrowserChrome::set_address(std::string address){if(address.empty())return false;m_address=std::move(address);return true;}
bool BrowserChrome::can_go_back()const{auto*s=m_application.active_session();return s&&s->can_go_back();}
bool BrowserChrome::can_go_forward()const{auto*s=m_application.active_session();return s&&s->can_go_forward();}
std::size_t BrowserChrome::tab_count()const{return m_application.tab_count();}std::size_t BrowserChrome::new_tab(){return m_application.new_tab();}
bool BrowserChrome::close_active_tab(){auto i=m_application.active_tab_index();return i&&m_application.close_tab(*i);}bool BrowserChrome::activate_tab(std::size_t i){return m_application.switch_to_tab(i);}
NavigationResult BrowserChrome::navigate(ResourceLoader& loader){m_state=PageState::Loading;NavigationRequest request;request.url=m_address;auto r=m_application.navigate(request,loader);sync_status(r);return r;}
NavigationResult BrowserChrome::reload(ResourceLoader& loader){m_state=PageState::Loading;auto r=m_application.reload(loader);sync_status(r);return r;}
NavigationResult BrowserChrome::back(ResourceLoader& loader){m_state=PageState::Loading;auto r=m_application.back(loader);sync_status(r);return r;}
NavigationResult BrowserChrome::forward(ResourceLoader& loader){m_state=PageState::Loading;auto r=m_application.forward(loader);sync_status(r);return r;}
void BrowserChrome::sync_status(NavigationResult const& r){if(r.error==NavigationError::None){m_state=PageState::Ready;m_status="Ready";}else{m_state=PageState::Error;m_status=r.message.empty()?"Navigation failed":r.message;}}
}
