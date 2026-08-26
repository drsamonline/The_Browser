#include <LibTest/TestCase.h>
#include "browser_application.hpp"
#include "browser_chrome.hpp"
using namespace aetheris::rendering;
TEST_CASE(basic_chrome_state){BrowserApplication app;BrowserChrome chrome(app);EXPECT_EQ(chrome.tab_count(),1u);EXPECT(chrome.set_address("https://example.test/"));EXPECT_EQ(chrome.address(),"https://example.test/");auto second=chrome.new_tab();EXPECT_EQ(chrome.tab_count(),2u);EXPECT(chrome.activate_tab(second));EXPECT(chrome.close_active_tab());EXPECT_EQ(chrome.tab_count(),1u);}
