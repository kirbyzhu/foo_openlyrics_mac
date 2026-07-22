#import "ConfigAdapter.h"
#import <Foundation/Foundation.h>

namespace openlyrics {

static NSString *const kConfigKey = @"foo_openlyrics_config";

AppConfig ConfigAdapter::load() const {
    NSUserDefaults *defs = [NSUserDefaults standardUserDefaults];
    NSString *json = [defs stringForKey:kConfigKey];
    if (json.length == 0) return AppConfig::defaults();
    return AppConfig::fromJson(json.UTF8String);
}

void ConfigAdapter::save(const AppConfig& config) const {
    NSUserDefaults *defs = [NSUserDefaults standardUserDefaults];
    [defs setObject:[NSString stringWithUTF8String:config.toJson().c_str()]
             forKey:kConfigKey];
}

}  // namespace openlyrics
