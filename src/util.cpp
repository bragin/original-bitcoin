// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"


bool fDebug = false;
bool fPrintToDebugger = false;
bool fPrintToConsole = false;




// Init openssl library multithreading support
static HANDLE* lock_cs;

void win32_locking_callback(int mode, int type, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
        WaitForSingleObject(lock_cs[type], INFINITE);
    else
        ReleaseMutex(lock_cs[type]);
}

// Init
class CInit
{
public:
    CInit()
    {
        // Init openssl library multithreading support
        lock_cs = (HANDLE*)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(HANDLE));
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            lock_cs[i] = CreateMutex(NULL,FALSE,NULL);
        CRYPTO_set_locking_callback(win32_locking_callback);

        // Seed random number generator with screen scrape and other hardware sources
        RAND_screen();

        // Seed random number generator with performance counter
        RandAddSeed();
    }
    ~CInit()
    {
        // Shutdown openssl library multithreading support
        CRYPTO_set_locking_callback(NULL);
        for (int i =0 ; i < CRYPTO_num_locks(); i++)
            CloseHandle(lock_cs[i]);
        OPENSSL_free(lock_cs);
    }
}
instance_of_cinit;




void RandAddSeed()
{
    // Seed with CPU performance counter
    LARGE_INTEGER PerformanceCount;
    QueryPerformanceCounter(&PerformanceCount);
    RAND_add(&PerformanceCount, sizeof(PerformanceCount), 1.5);
    memset(&PerformanceCount, 0, sizeof(PerformanceCount));
}

void RandAddSeedPerfmon()
{
    // This can take up to 2 seconds, so only do it every 10 minutes
    static int64 nLastPerfmon;
    if (GetTime() < nLastPerfmon + 10 * 60)
        return;
    nLastPerfmon = GetTime();

    // Seed with the entire set of perfmon data
    unsigned char pdata[250000];
    memset(pdata, 0, sizeof(pdata));
    unsigned long nSize = sizeof(pdata);
    long ret = RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Global", NULL, NULL, pdata, &nSize);
    RegCloseKey(HKEY_PERFORMANCE_DATA);
    if (ret == ERROR_SUCCESS)
    {
        uint256 hash;
        SHA256(pdata, nSize, (unsigned char*)&hash);
        RAND_add(&hash, sizeof(hash), min(nSize/500.0, (double)sizeof(hash)));
        hash = 0;
        memset(pdata, 0, nSize);

        time_t nTime;
        time(&nTime);
        struct tm* ptmTime = gmtime(&nTime);
        char pszTime[200];
        strftime(pszTime, sizeof(pszTime), "%x %H:%M:%S", ptmTime);
        printf("%s RandAddSeed() %d bytes\n", pszTime, nSize);
    }
}









// Safer snprintf
//  - prints up to limit-1 characters
//  - output string is always null terminated even if limit reached
//  - return value is the number of characters actually printed
int my_snprintf(char* buffer, size_t limit, const char* format, ...)
{
    if (limit == 0)
        return 0;
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int ret = _vsnprintf(buffer, limit, format, arg_ptr);
    va_end(arg_ptr);
    if (ret < 0 || ret >= limit)
    {
        ret = limit - 1;
        buffer[limit-1] = 0;
    }
    return ret;
}


string strprintf(const char* format, ...)
{
    char buffer[50000];
    char* p = buffer;
    int limit = sizeof(buffer);
    int ret;
    loop
    {
        va_list arg_ptr;
        va_start(arg_ptr, format);
        ret = _vsnprintf(p, limit, format, arg_ptr);
        va_end(arg_ptr);
        if (ret >= 0 && ret < limit)
            break;
        if (p != buffer)
            delete p;
        limit *= 2;
        p = new char[limit];
        if (p == NULL)
            throw std::bad_alloc();
    }
#ifdef _MSC_VER
    // msvc optimisation
    if (p == buffer)
        return string(p, p+ret);
#endif
    string str(p, p+ret);
    if (p != buffer)
        delete p;
    return str;
}


bool error(const char* format, ...)
{
    char buffer[50000];
    int limit = sizeof(buffer);
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int ret = _vsnprintf(buffer, limit, format, arg_ptr);
    va_end(arg_ptr);
    if (ret < 0 || ret >= limit)
    {
        ret = limit - 1;
        buffer[limit-1] = 0;
    }
    printf("ERROR: %s\n", buffer);
    return false;
}


void ParseString(const string& str, char c, vector<string>& v)
{
    if (str.empty())
        return;
    string::size_type i1 = 0;
    string::size_type i2;
    loop
    {
        i2 = str.find(c, i1);
        if (i2 == str.npos)
        {
            v.push_back(str.substr(i1));
            return;
        }
        v.push_back(str.substr(i1, i2-i1));
        i1 = i2+1;
    }
}


string FormatMoney(int64 n, bool fPlus)
{
    n /= CENT;
    string str = strprintf("%I64d.%02I64d", (n > 0 ? n : -n)/100, (n > 0 ? n : -n)%100);
    for (int i = 6; i < str.size(); i += 4)
        if (isdigit(str[str.size() - i - 1]))
            str.insert(str.size() - i, 1, ',');
    if (n < 0)
        str.insert((unsigned int)0, 1, '-');
    else if (fPlus && n > 0)
        str.insert((unsigned int)0, 1, '+');
    return str;
}

bool ParseMoney(const char* pszIn, int64& nRet)
{
    string strWhole;
    int64 nCents = 0;
    const char* p = pszIn;
    while (isspace(*p))
        p++;
    for (; *p; p++)
    {
        if (*p == ',' && p > pszIn && isdigit(p[-1]) && isdigit(p[1]) && isdigit(p[2]) && isdigit(p[3]) && !isdigit(p[4]))
            continue;
        if (*p == '.')
        {
            p++;
            if (isdigit(*p))
            {
                nCents = 10 * (*p++ - '0');
                if (isdigit(*p))
                    nCents += (*p++ - '0');
            }
            break;
        }
        if (isspace(*p))
            break;
        if (!isdigit(*p))
            return false;
        strWhole.insert(strWhole.end(), *p);
    }
    for (; *p; p++)
        if (!isspace(*p))
            return false;
    if (strWhole.size() > 14)
        return false;
    if (nCents < 0 || nCents > 99)
        return false;
    int64 nWhole = atoi64(strWhole);
    int64 nPreValue = nWhole * 100 + nCents;
    int64 nValue = nPreValue * CENT;
    if (nValue / CENT != nPreValue)
        return false;
    if (nValue / COIN != nWhole)
        return false;
    nRet = nValue;
    return true;
}


vector<unsigned char> ParseHex(const char* psz)
{
    vector<unsigned char> vch;
    while (isspace(*psz))
        psz++;
    vch.reserve((strlen(psz)+1)/3);

    static char phexdigit[256] =
    { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,
      -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, };

    while (*psz)
    {
        char c = phexdigit[(unsigned char)*psz++];
        if (c == -1)
            break;
        unsigned char n = (c << 4);
        if (*psz)
        {
            char c = phexdigit[(unsigned char)*psz++];
            if (c == -1)
                break;
            n |= c;
            vch.push_back(n);
        }
        while (isspace(*psz))
            psz++;
    }

    return vch;
}

vector<unsigned char> ParseHex(const std::string& str)
{
    return ParseHex(str.c_str());
}






void FormatException(char* pszMessage, std::exception* pex, const char* pszThread)
{
    char pszModule[MAX_PATH];
    pszModule[0] = '\0';
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
    if (pex)
        snprintf(pszMessage, 1000,
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        snprintf(pszMessage, 1000,
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void LogException(std::exception* pex, const char* pszThread)
{
    char pszMessage[1000];
    FormatException(pszMessage, pex, pszThread);
    printf("\n%s", pszMessage);
}

void PrintException(std::exception* pex, const char* pszThread)
{
    char pszMessage[1000];
    FormatException(pszMessage, pex, pszThread);
    printf("\n\n************************\n%s\n", pszMessage);
    if (wxTheApp)
        wxMessageBox(pszMessage, "Error", wxOK | wxICON_ERROR);
    throw;
    //DebugBreak();
}








bool FileExists(const char* psz)
{
#ifdef WIN32
    return GetFileAttributesA(psz) != -1;
#else
    return access(psz, 0) != -1;
#endif
}

int GetFilesize(FILE* file)
{
    int nSavePos = ftell(file);
    int nFilesize = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        nFilesize = ftell(file);
    fseek(file, nSavePos, SEEK_SET);
    return nFilesize;
}








uint64 GetRand(uint64 nMax)
{
    if (nMax == 0)
        return 0;

    // The range of the random source must be a multiple of the modulus
    // to give every possible output value an equal possibility
    uint64 nRange = (_UI64_MAX / nMax) * nMax;
    uint64 nRand = 0;
    do
        RAND_bytes((unsigned char*)&nRand, sizeof(nRand));
    while (nRand >= nRange);
    return (nRand % nMax);
}










//
// "Never go to sea with two chronometers; take one or three."
// Our three chronometers are:
//  - System clock
//  - Median of other server's clocks
//  - NTP servers
//
// note: NTP isn't implemented yet, so until then we just use the median
//  of other nodes clocks to correct ours.
//

int64 GetTime()
{
    return time(NULL);
}

static int64 nTimeOffset = 0;

int64 GetAdjustedTime()
{
    return GetTime() + nTimeOffset;
}

void AddTimeData(unsigned int ip, int64 nTime)
{
    int64 nOffsetSample = nTime - GetTime();

    // Ignore duplicates
    static set<unsigned int> setKnown;
    if (!setKnown.insert(ip).second)
        return;

    // Add data
    static vector<int64> vTimeOffsets;
    if (vTimeOffsets.empty())
        vTimeOffsets.push_back(0);
    vTimeOffsets.push_back(nOffsetSample);
    printf("Added time data, samples %d, offset %+I64d (%+I64d minutes)\n", vTimeOffsets.size(), vTimeOffsets.back(), vTimeOffsets.back()/60);
    if (vTimeOffsets.size() >= 5 && vTimeOffsets.size() % 2 == 1)
    {
        sort(vTimeOffsets.begin(), vTimeOffsets.end());
        int64 nMedian = vTimeOffsets[vTimeOffsets.size()/2];
        nTimeOffset = nMedian;
        if ((nMedian > 0 ? nMedian : -nMedian) > 5 * 60)
        {
            // Only let other nodes change our clock so far before we
            // go to the NTP servers
            /// todo: Get time from NTP servers, then set a flag
            ///    to make sure it doesn't get changed again
        }
        foreach(int64 n, vTimeOffsets)
            printf("%+I64d  ", n);
        printf("|  nTimeOffset = %+I64d  (%+I64d minutes)\n", nTimeOffset, nTimeOffset/60);
    }
}
