// HTTP Date Parser for Defold HTTP Cache
// Handles RFC 7231 date formats for Expires header parsing
// 
// Supports three formats:
// 1. RFC 1123: "Wed, 21 Oct 2026 07:28:00 GMT"
// 2. RFC 850:  "Wednesday, 21-Oct-26 07:28:00 GMT"  
// 3. ANSI C:   "Wed Oct 21 07:28:00 2026"

#include <time.h>
#include <string.h>
#include <ctype.h>

// Platform compatibility: timegm is not available on all platforms
#if defined(_WIN32) || defined(_WIN64)
    // Windows doesn't have timegm, use _mkgmtime
    #define timegm _mkgmtime
#endif

/**
 * Parse an HTTP date string into a Unix timestamp.
 * 
 * @param date_str The date string from the Expires header
 * @return Unix timestamp (seconds since epoch), or 0 if parsing fails
 * 
 * @note Returns 0 for invalid dates, which signals "don't cache"
 * @see RFC 7231 Section 7.1.1.1 for date format specifications
 */
static time_t ParseHttpDate(const char* date_str)
{
    // Null or empty string = invalid
    if (date_str == NULL || *date_str == '\0') {
        return 0;
    }

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    
    // HTTP dates must be in GMT (no timezone conversion needed)
    
    // ----------------------------------------------------------------
    // TRY FORMAT 1: RFC 1123 (preferred, most common)
    // Example: "Wed, 21 Oct 2026 07:28:00 GMT"
    // Format:  "%a, %d %b %Y %H:%M:%S GMT"
    // ----------------------------------------------------------------
#if !defined(_WIN32) && !defined(_WIN64)
    // Unix/Linux/Mac: use strptime
    if (strptime(date_str, "%a, %d %b %Y %H:%M:%S GMT", &tm) != NULL) {
        time_t result = timegm(&tm);
        if (result != (time_t)-1) {
            return result;
        }
    }
    
    // ----------------------------------------------------------------
    // TRY FORMAT 2: RFC 850 (obsolete but still seen in the wild)
    // Example: "Wednesday, 21-Oct-26 07:28:00 GMT"
    // Format:  "%A, %d-%b-%y %H:%M:%S GMT"
    // ----------------------------------------------------------------
    memset(&tm, 0, sizeof(tm));
    if (strptime(date_str, "%A, %d-%b-%y %H:%M:%S GMT", &tm) != NULL) {
        // Two-digit year: 00-68 = 2000-2068, 69-99 = 1969-1999
        // This is handled automatically by strptime for %y
        time_t result = timegm(&tm);
        if (result != (time_t)-1) {
            return result;
        }
    }
    
    // ----------------------------------------------------------------
    // TRY FORMAT 3: ANSI C asctime() format (rarely used)
    // Example: "Wed Oct 21 07:28:00 2026"
    // Format:  "%a %b %d %H:%M:%S %Y"
    // ----------------------------------------------------------------
    memset(&tm, 0, sizeof(tm));
    if (strptime(date_str, "%a %b %d %H:%M:%S %Y", &tm) != NULL) {
        time_t result = timegm(&tm);
        if (result != (time_t)-1) {
            return result;
        }
    }
#else
    // ----------------------------------------------------------------
    // WINDOWS IMPLEMENTATION (strptime not available)
    // Manual parsing using sscanf
    // ----------------------------------------------------------------
    char day_name[16];
    char month_name[16];
    int day, year, hour, minute, second;
    
    // Try RFC 1123: "Wed, 21 Oct 2026 07:28:00 GMT"
    if (sscanf(date_str, "%3s, %d %3s %d %d:%d:%d GMT",
               day_name, &day, month_name, &year, &hour, &minute, &second) == 7) {
        
        tm.tm_mday = day;
        tm.tm_year = year - 1900;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        tm.tm_mon = ParseMonthName(month_name);
        
        if (tm.tm_mon >= 0) {
            time_t result = timegm(&tm);
            if (result != (time_t)-1) {
                return result;
            }
        }
    }
    
    // Try RFC 850: "Wednesday, 21-Oct-26 07:28:00 GMT"
    memset(&tm, 0, sizeof(tm));
    if (sscanf(date_str, "%15[^,], %d-%3s-%d %d:%d:%d GMT",
               day_name, &day, month_name, &year, &hour, &minute, &second) == 7) {
        
        tm.tm_mday = day;
        // Two-digit year handling: 00-68 = 2000-2068, 69-99 = 1969-1999
        tm.tm_year = (year <= 68) ? (year + 100) : year;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        tm.tm_mon = ParseMonthName(month_name);
        
        if (tm.tm_mon >= 0) {
            time_t result = timegm(&tm);
            if (result != (time_t)-1) {
                return result;
            }
        }
    }
    
    // Try ANSI C: "Wed Oct 21 07:28:00 2026"
    memset(&tm, 0, sizeof(tm));
    if (sscanf(date_str, "%3s %3s %d %d:%d:%d %d",
               day_name, month_name, &day, &hour, &minute, &second, &year) == 7) {
        
        tm.tm_mday = day;
        tm.tm_year = year - 1900;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        tm.tm_mon = ParseMonthName(month_name);
        
        if (tm.tm_mon >= 0) {
            time_t result = timegm(&tm);
            if (result != (time_t)-1) {
                return result;
            }
        }
    }
#endif
    
    // All parsing attempts failed
    // Use Defold's logging if available, otherwise silent failure
    #ifdef DM_LOG_WARNING
        dmLogWarning("HTTP Cache: Failed to parse Expires header: '%s'", date_str);
    #endif
    
    return 0;  // Return 0 = "don't cache this"
}

#if defined(_WIN32) || defined(_WIN64)
/**
 * Helper function for Windows: Convert month name to tm_mon value (0-11)
 * 
 * @param month_name Three-letter month abbreviation (e.g., "Jan", "Feb")
 * @return Month number (0-11), or -1 if invalid
 */
static int ParseMonthName(const char* month_name)
{
    if (month_name == NULL) return -1;
    
    // Convert to lowercase for comparison
    char lower[4] = {0};
    for (int i = 0; i < 3 && month_name[i]; i++) {
        lower[i] = tolower(month_name[i]);
    }
    
    // Month lookup table
    const char* months[] = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec"
    };
    
    for (int i = 0; i < 12; i++) {
        if (strcmp(lower, months[i]) == 0) {
            return i;
        }
    }
    
    return -1;  // Invalid month name
}
#endif

// ============================================================================
// USAGE EXAMPLES (for reference - not part of the actual implementation)
// ============================================================================
#if 0

// Example 1: Basic usage in HTTP response handler
void HandleHttpResponse(HttpResponse* response) {
    const char* expires = GetHeader(response, "Expires");
    
    if (expires != NULL) {
        time_t expires_timestamp = ParseHttpDate(expires);
        
        if (expires_timestamp > 0) {
            // Valid date parsed
            time_t now = time(NULL);
            
            if (now < expires_timestamp) {
                // Cache is still fresh!
                cache_entry->m_ExpiresAt = expires_timestamp;
                dmLogInfo("Cached until: %ld", expires_timestamp);
            } else {
                // Already expired, don't cache
                dmLogDebug("Expires header is already past");
            }
        } else {
            // Invalid date format, treat as no expiration
            dmLogWarning("Invalid Expires header format");
        }
    }
}

// Example 2: Testing with different date formats
void TestDateParsing() {
    time_t result;
    
    // RFC 1123 format (most common)
    result = ParseHttpDate("Wed, 21 Oct 2026 07:28:00 GMT");
    assert(result > 0);
    
    // RFC 850 format (obsolete)
    result = ParseHttpDate("Wednesday, 21-Oct-26 07:28:00 GMT");
    assert(result > 0);
    
    // ANSI C format (rare)
    result = ParseHttpDate("Wed Oct 21 07:28:00 2026");
    assert(result > 0);
    
    // Invalid dates should return 0
    result = ParseHttpDate("Invalid Date String");
    assert(result == 0);
    
    result = ParseHttpDate(NULL);
    assert(result == 0);
}

// Example 3: Integration with cache freshness check
bool IsCacheFresh(CacheEntry* entry) {
    time_t now = time(NULL);
    
    // Check max-age first (takes precedence per RFC 7234)
    if (entry->m_MaxAge > 0) {
        time_t age = now - entry->m_CreatedAt;
        return age < entry->m_MaxAge;
    }
    
    // Fall back to Expires header (parsed by ParseHttpDate)
    if (entry->m_ExpiresAt > 0) {
        return now < entry->m_ExpiresAt;
    }
    
    // No expiration info = treat as stale
    return false;
}

#endif

// ============================================================================
// EDGE CASES HANDLED
// ============================================================================
/*
 * 1. NULL pointer → returns 0
 * 2. Empty string → returns 0  
 * 3. Invalid date format → returns 0
 * 4. Dates in the past → returns valid timestamp (caller decides what to do)
 * 5. Far future dates → returns valid timestamp (system-dependent limit)
 * 6. Two-digit years in RFC 850 → correctly maps to 1900s or 2000s
 * 7. Platform differences → Windows and Unix implementations
 * 8. Timezone → assumes GMT (as per HTTP spec)
 */
