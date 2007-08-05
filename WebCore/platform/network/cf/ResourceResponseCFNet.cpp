/*
 * Copyright (C) 2006, 2007 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ResourceResponseCFNet.h"

#include "HTTPParsers.h"
#include "MIMETypeRegistry.h"
#include "ResourceResponse.h"
#include <CFNetwork/CFURLResponsePriv.h>
#include <wtf/RetainPtr.h>

using std::min;

// We would like a better value for a maximum time_t,
// but there is no way to do that in C with any certainty.
// INT_MAX should work well enough for our purposes.
#define MAX_TIME_T ((time_t)INT_MAX)    

namespace WebCore {

CFURLResponseRef ResourceResponse::cfURLResponse() const
{  
    return m_cfResponse.get();
}

static inline bool filenameHasSaneExtension(const String& filename)
{
    int dot = filename.find('.');

    // The dot can't be the first or last character in the filename.
    int length = filename.length();
    return dot > 0 && dot < length - 1;
}

static inline String suggestedFilenameForResponse(const ResourceResponse& response, const HTTPHeaderMap& headers)
{
    // FIXME: When <rdar://problem/5053780> is fixed we can get rid of this function.
    CFURLResponseRef cfURLResponse = response.cfURLResponse();
    ASSERT(cfURLResponse);

    String filename;

    // First, try the Content-Disposition header.
    String contentDisposition = headers.get("Content-Disposition");
    if (!contentDisposition.isNull())
        filename = filenameFromHTTPContentDisposition(contentDisposition);

    if (filename.isNull()) {
        RetainPtr<CFStringRef> suggestedFilename(AdoptCF, CFURLResponseCopySuggestedFilename(cfURLResponse));

        filename = suggestedFilename.get();
    }
    
    return filename;
}

void ResourceResponse::doUpdateResourceResponse()
{
    if (!m_cfResponse.get())
        return;

    // FIXME: We may need to do MIME type sniffing here (unless that is done in CFURLResponseGetMIMEType).

    m_url = CFURLResponseGetURL(m_cfResponse.get());
    m_mimeType = CFURLResponseGetMIMEType(m_cfResponse.get());
    m_expectedContentLength = CFURLResponseGetExpectedContentLength(m_cfResponse.get());
    m_textEncodingName = CFURLResponseGetTextEncodingName(m_cfResponse.get());

    CFAbsoluteTime expiration = CFURLResponseGetExpirationTime(m_cfResponse.get());
    m_expirationDate = min((time_t)(expiration + kCFAbsoluteTimeIntervalSince1970), MAX_TIME_T);

    CFAbsoluteTime lastModified = CFURLResponseGetLastModifiedDate(m_cfResponse.get());
    m_lastModifiedDate = min((time_t)(lastModified + kCFAbsoluteTimeIntervalSince1970), MAX_TIME_T);

    CFHTTPMessageRef httpResponse = CFURLResponseGetHTTPResponse(m_cfResponse.get());
    if (httpResponse) {
        m_httpStatusCode = CFHTTPMessageGetResponseStatusCode(httpResponse);

        RetainPtr<CFStringRef> statusLine(AdoptCF, CFHTTPMessageCopyResponseStatusLine(httpResponse));
        String statusText(statusLine.get());
        int spacePos = statusText.find(" ");
        if (spacePos != -1)
            statusText = statusText.substring(spacePos + 1);
        m_httpStatusText = statusText;

        RetainPtr<CFDictionaryRef> headers(AdoptCF, CFHTTPMessageCopyAllHeaderFields(httpResponse));
        CFIndex headerCount = CFDictionaryGetCount(headers.get());
        Vector<const void*, 128> keys(headerCount);
        Vector<const void*, 128> values(headerCount);
        CFDictionaryGetKeysAndValues(headers.get(), keys.data(), values.data());
        for (int i = 0; i < headerCount; ++i)
            m_httpHeaderFields.set((CFStringRef)keys[i], (CFStringRef)values[i]);
    } else
        m_httpStatusCode = 0;

    m_suggestedFilename = suggestedFilenameForResponse(m_cfResponse.get(), m_httpHeaderFields);

    // FIXME: This is a workaround for <rdar://problem/5386894> CFURLResponseGetMIMEType returns
    // text/html for local .xhtml and .xml files. This code should be removed once that bug is fixed.
    if (m_url.isLocalFile() && m_mimeType == "text/html") {
        const String& path = m_url.path();
        static const String xhtmlExt(".xhtml");
        static const String xmlExt(".xml");
        if (path.endsWith(xhtmlExt, false))
            m_mimeType = "application/xhtml+xml";
        else if (path.endsWith(xmlExt, false))
            m_mimeType = "text/xml";
    }
}

}
