#pragma once

namespace bench {

using u64 = __UINT64_TYPE__;
using u32 = __UINT32_TYPE__;
using u16 = __UINT16_TYPE__;
using u8  = __UINT8_TYPE__;

using i64 = __INT64_TYPE__;
using i32 = __INT32_TYPE__;
using i16 = __INT16_TYPE__;
using i8  = __INT8_TYPE__;

using bits_t      = u32;
using bytes_t     = u32;
using code_path_t = u16;
using addr_t      = u64;

using time_s_t  = i64;
using time_ms_t = i64;
using time_us_t = i64;
using time_ns_t = i64;

} // namespace bench