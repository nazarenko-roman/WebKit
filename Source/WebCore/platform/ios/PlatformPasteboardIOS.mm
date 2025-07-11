/*
 * Copyright (C) 2013-2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "PlatformPasteboard.h"

#if PLATFORM(IOS_FAMILY)

#import "ColorCocoa.h"
#import "CommonAtomStrings.h"
#import "Image.h"
#import "NSURLUtilities.h"
#import "Pasteboard.h"
#import "SharedBuffer.h"
#import "UTIUtilities.h"
#import "WebItemProviderPasteboard.h"
#import <MobileCoreServices/MobileCoreServices.h>
#import <UIKit/UIColor.h>
#import <UIKit/UIImage.h>
#import <UIKit/UIPasteboard.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <pal/spi/ios/UIKitSPI.h>
#import <wtf/ListHashSet.h>
#import <wtf/URL.h>
#import <wtf/cocoa/NSURLExtras.h>
#import <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#import <wtf/cocoa/TypeCastsCocoa.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/text/StringHash.h>

#import <pal/ios/UIKitSoftLink.h>

#define PASTEBOARD_SUPPORTS_ITEM_PROVIDERS (PLATFORM(IOS_FAMILY) && !(PLATFORM(WATCHOS) || PLATFORM(APPLETV)))
#define PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA (PASTEBOARD_SUPPORTS_ITEM_PROVIDERS && !PLATFORM(MACCATALYST))

@interface UIPasteboard () <AbstractPasteboard>
@end

namespace WebCore {

static UIPasteboard *generalUIPasteboard()
{
    return static_cast<UIPasteboard *>([PAL::getUIPasteboardClass() generalPasteboard]);
}

PlatformPasteboard::PlatformPasteboard()
    : m_pasteboard(generalUIPasteboard())
{
}

#if PASTEBOARD_SUPPORTS_ITEM_PROVIDERS
PlatformPasteboard::PlatformPasteboard(const String& name)
{
    if (name == Pasteboard::nameOfDragPasteboard())
        m_pasteboard = [WebItemProviderPasteboard sharedInstance];
    else
        m_pasteboard = generalUIPasteboard();
}
#else
PlatformPasteboard::PlatformPasteboard(const String&)
    : m_pasteboard(generalUIPasteboard())
{
}
#endif

void PlatformPasteboard::getTypes(Vector<String>& types) const
{
    types = makeVector<String>([m_pasteboard pasteboardTypes]);
}

PasteboardBuffer PlatformPasteboard::bufferForType(const String& type) const
{
    PasteboardBuffer pasteboardBuffer;
    pasteboardBuffer.data = readBuffer(0, type);
    pasteboardBuffer.type = type;
    return pasteboardBuffer;
}

void PlatformPasteboard::performAsDataOwner(DataOwnerType type, NOESCAPE Function<void()>&& actions)
{
    auto dataOwner = _UIDataOwnerUndefined;
    switch (type) {
    case DataOwnerType::Undefined:
        dataOwner = _UIDataOwnerUndefined;
        break;
    case DataOwnerType::User:
        dataOwner = _UIDataOwnerUser;
        break;
    case DataOwnerType::Enterprise:
        dataOwner = _UIDataOwnerEnterprise;
        break;
    case DataOwnerType::Shared:
        dataOwner = _UIDataOwnerShared;
        break;
    }

    [PAL::getUIPasteboardClass() _performAsDataOwner:dataOwner block:^{
        actions();
    }];
}

void PlatformPasteboard::getPathnamesForType(Vector<String>&, const String&) const
{
}

int PlatformPasteboard::numberOfFiles() const
{
    return [m_pasteboard respondsToSelector:@selector(numberOfFiles)] ? [m_pasteboard numberOfFiles] : 0;
}

static bool shouldTreatAtLeastOneTypeAsFile(NSArray<NSString *> *platformTypes)
{
    for (NSString *type in platformTypes) {
        if (Pasteboard::shouldTreatCocoaTypeAsFile(type))
            return true;
    }
    return false;
}

#if PASTEBOARD_SUPPORTS_ITEM_PROVIDERS

static const char *safeTypeForDOMToReadAndWriteForPlatformType(const String& platformType, PlatformPasteboard::IncludeImageTypes includeImageTypes)
{
    auto cfType = platformType.createCFString();
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (UTTypeConformsTo(cfType.get(), kUTTypePlainText))
        return "text/plain"_s;

    if (UTTypeConformsTo(cfType.get(), kUTTypeHTML) || UTTypeConformsTo(cfType.get(), (CFStringRef)WebArchivePboardType)
        || UTTypeConformsTo(cfType.get(), kUTTypeRTF) || UTTypeConformsTo(cfType.get(), kUTTypeFlatRTFD))
        return "text/html"_s;

    if (UTTypeConformsTo(cfType.get(), kUTTypeURL))
        return "text/uri-list"_s;

    if (includeImageTypes == PlatformPasteboard::IncludeImageTypes::Yes && UTTypeConformsTo(cfType.get(), kUTTypePNG))
        return "image/png"_s;
ALLOW_DEPRECATED_DECLARATIONS_END

    return nullptr;
}

static Vector<String> webSafeTypes(NSArray<NSString *> *platformTypes, PlatformPasteboard::IncludeImageTypes includeImageTypes, Function<bool()>&& shouldAvoidExposingURLType)
{
    ListHashSet<String> domPasteboardTypes;
    for (NSString *type in platformTypes) {
        if ([type isEqualToString:@(PasteboardCustomData::cocoaType().characters())])
            continue;

        if (Pasteboard::isSafeTypeForDOMToReadAndWrite(type)) {
            domPasteboardTypes.add(type);
            continue;
        }

        if (auto* coercedType = safeTypeForDOMToReadAndWriteForPlatformType(type, includeImageTypes)) {
            auto domTypeAsString = String::fromUTF8(coercedType);
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if (domTypeAsString == "text/uri-list"_s && ([platformTypes containsObject:(__bridge NSString *)kUTTypeFileURL] || shouldAvoidExposingURLType()))
                continue;
ALLOW_DEPRECATED_DECLARATIONS_END

            domPasteboardTypes.add(WTFMove(domTypeAsString));
        }
    }
    return copyToVector(domPasteboardTypes);
}

#if PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA

static PasteboardItemPresentationStyle pasteboardItemPresentationStyle(UIPreferredPresentationStyle style)
{
    switch (style) {
    case UIPreferredPresentationStyleUnspecified:
        return PasteboardItemPresentationStyle::Unspecified;
    case UIPreferredPresentationStyleInline:
        return PasteboardItemPresentationStyle::Inline;
    case UIPreferredPresentationStyleAttachment:
        return PasteboardItemPresentationStyle::Attachment;
    default:
        ASSERT_NOT_REACHED();
        return PasteboardItemPresentationStyle::Unspecified;
    }
}

#endif // PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA

std::optional<PasteboardItemInfo> PlatformPasteboard::informationForItemAtIndex(size_t index, int64_t changeCount)
{
    if (index >= static_cast<NSUInteger>([m_pasteboard numberOfItems]))
        return std::nullopt;

    if (this->changeCount() != changeCount)
        return std::nullopt;

    PasteboardItemInfo info;
    NSItemProvider *itemProvider = [[m_pasteboard itemProviders] objectAtIndex:index];
    if ([m_pasteboard respondsToSelector:@selector(fileUploadURLsAtIndex:fileTypes:)]) {
        NSArray<NSString *> *fileTypes = nil;
        NSArray *urls = [m_pasteboard fileUploadURLsAtIndex:index fileTypes:&fileTypes];
        ASSERT(fileTypes.count == urls.count);

        info.pathsForFileUpload.reserveInitialCapacity(urls.count);
        for (NSURL *url in urls)
            info.pathsForFileUpload.append(url.path);

        info.platformTypesForFileUpload.reserveInitialCapacity(fileTypes.count);
        for (NSString *fileType in fileTypes)
            info.platformTypesForFileUpload.append(fileType);
    } else {
        NSArray *fileTypes = itemProvider.web_fileUploadContentTypes;
        info.platformTypesForFileUpload.reserveInitialCapacity(fileTypes.count);
        info.pathsForFileUpload.reserveInitialCapacity(fileTypes.count);
        for (NSString *fileType in fileTypes) {
            info.platformTypesForFileUpload.append(fileType);
            info.pathsForFileUpload.append({ });
        }
    }

#if PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA
    info.preferredPresentationStyle = pasteboardItemPresentationStyle(itemProvider.preferredPresentationStyle);
#endif
    if (!CGSizeEqualToSize(itemProvider.preferredPresentationSize, CGSizeZero)) {
        auto adjustedPreferredPresentationHeight = [](auto height) -> std::optional<double> {
            if (!WTF::IOSApplication::isMobileMail() && !WTF::IOSApplication::isMailCompositionService())
                return { height };
            // Mail's max-width: 100%; default style is in conflict with the preferred presentation size and can lead to unexpectedly stretched images. Not setting the height forces layout to preserve the aspect ratio.
            return { };
        };
        info.preferredPresentationSize = PresentationSize { itemProvider.preferredPresentationSize.width, adjustedPreferredPresentationHeight(itemProvider.preferredPresentationSize.height) };
    }
    info.containsFileURLAndFileUploadContent = itemProvider.web_containsFileURLAndFileUploadContent;
    info.suggestedFileName = itemProvider.suggestedName;
    NSArray<NSString *> *registeredTypeIdentifiers = itemProvider.registeredTypeIdentifiers;
    info.platformTypesByFidelity.reserveInitialCapacity(registeredTypeIdentifiers.count);
    for (NSString *typeIdentifier in registeredTypeIdentifiers) {
        info.platformTypesByFidelity.append(typeIdentifier);
        CFStringRef cfTypeIdentifier = (CFStringRef)typeIdentifier;
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if (!UTTypeIsDeclared(cfTypeIdentifier))
            continue;

        if (UTTypeConformsTo(cfTypeIdentifier, kUTTypeText))
            continue;

        if (UTTypeConformsTo(cfTypeIdentifier, kUTTypeURL))
            continue;

        if (UTTypeConformsTo(cfTypeIdentifier, kUTTypeRTFD))
            continue;

        if (UTTypeConformsTo(cfTypeIdentifier, kUTTypeFlatRTFD))
            continue;
ALLOW_DEPRECATED_DECLARATIONS_END

        info.isNonTextType = true;
    }

    info.webSafeTypesByFidelity = webSafeTypes(registeredTypeIdentifiers, IncludeImageTypes::Yes, [&] {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        return shouldTreatAtLeastOneTypeAsFile(registeredTypeIdentifiers) && !Pasteboard::canExposeURLToDOMWhenPasteboardContainsFiles(readString(index, kUTTypeURL));
ALLOW_DEPRECATED_DECLARATIONS_END
    });

    return info;
}

#else

std::optional<PasteboardItemInfo> PlatformPasteboard::informationForItemAtIndex(size_t, int64_t)
{
    return std::nullopt;
}

#endif

static bool pasteboardMayContainFilePaths(id<AbstractPasteboard> pasteboard)
{
#if PASTEBOARD_SUPPORTS_ITEM_PROVIDERS
    if ([pasteboard isKindOfClass:[WebItemProviderPasteboard class]])
        return false;
#endif
    return shouldTreatAtLeastOneTypeAsFile(pasteboard.pasteboardTypes);
}

String PlatformPasteboard::stringForType(const String& type) const
{
    auto result = readString(0, type);

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (pasteboardMayContainFilePaths(m_pasteboard.get()) && type == String { kUTTypeURL }) {
        if (!Pasteboard::canExposeURLToDOMWhenPasteboardContainsFiles(result))
            result = { };
    }
ALLOW_DEPRECATED_DECLARATIONS_END

    return result;
}

Color PlatformPasteboard::color()
{
    NSData *data = [m_pasteboard dataForPasteboardType:UIColorPboardType];
    UIColor *uiColor = [NSKeyedUnarchiver unarchivedObjectOfClass:PAL::getUIColorClass() fromData:data error:nil];
    return roundAndClampToSRGBALossy(uiColor.CGColor);
}

URL PlatformPasteboard::url()
{
    return URL();
}

int64_t PlatformPasteboard::copy(const String&)
{
    return 0;
}

int64_t PlatformPasteboard::addTypes(const Vector<String>&)
{
    return 0;
}

int64_t PlatformPasteboard::setTypes(const Vector<String>&, PasteboardDataLifetime)
{
    return 0;
}

int64_t PlatformPasteboard::setBufferForType(SharedBuffer*, const String&)
{
    return 0;
}

int64_t PlatformPasteboard::setURL(const PasteboardURL&)
{
    return 0;
}

int64_t PlatformPasteboard::setStringForType(const String&, const String&)
{
    return 0;
}

int64_t PlatformPasteboard::changeCount() const
{
    return [m_pasteboard changeCount];
}

String PlatformPasteboard::platformPasteboardTypeForSafeTypeForDOMToReadAndWrite(const String& domType, IncludeImageTypes includeImageTypes)
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (domType == textPlainContentTypeAtom())
        return kUTTypePlainText;

    if (domType == textHTMLContentTypeAtom())
        return kUTTypeHTML;

    if (domType == "text/uri-list"_s)
        return kUTTypeURL;

    if (includeImageTypes == IncludeImageTypes::Yes && domType == "image/png"_s)
        return kUTTypePNG;
ALLOW_DEPRECATED_DECLARATIONS_END

    return { };
}

#if PASTEBOARD_SUPPORTS_ITEM_PROVIDERS

static NSString *webIOSPastePboardType = @"iOS rich content paste pasteboard type";

static void registerItemsToPasteboard(NSArray<WebItemProviderRegistrationInfoList *> *itemLists, id <AbstractPasteboard> pasteboard)
{
#if PLATFORM(MACCATALYST)
    // In macCatalyst, -[UIPasteboard setItemProviders:] is not yet supported, so we fall back to setting an item dictionary when
    // populating the pasteboard upon copy.
    if ([pasteboard isKindOfClass:PAL::getUIPasteboardClass()]) {
        auto itemDictionaries = adoptNS([[NSMutableArray alloc] initWithCapacity:itemLists.count]);
        for (WebItemProviderRegistrationInfoList *representationsToRegister in itemLists) {
            auto itemDictionary = adoptNS([[NSMutableDictionary alloc] initWithCapacity:representationsToRegister.numberOfItems]);
            [representationsToRegister enumerateItems:[itemDictionary] (id <WebItemProviderRegistrar> item, NSUInteger) {
                if ([item respondsToSelector:@selector(typeIdentifierForClient)] && [item respondsToSelector:@selector(dataForClient)])
                    [itemDictionary setObject:item.dataForClient forKey:item.typeIdentifierForClient];
            }];
            [itemDictionaries addObject:itemDictionary.get()];
        }
        [pasteboard setItems:itemDictionaries.get()];
        return;
    }
#endif // PLATFORM(MACCATALYST)

    auto itemProviders = adoptNS([[NSMutableArray alloc] initWithCapacity:itemLists.count]);
    for (WebItemProviderRegistrationInfoList *representationsToRegister in itemLists) {
        if (auto *itemProvider = representationsToRegister.itemProvider)
            [itemProviders addObject:itemProvider];
    }

    [pasteboard setItemProviders:itemProviders.get()];

    if ([pasteboard respondsToSelector:@selector(stageRegistrationLists:)])
        [pasteboard stageRegistrationLists:itemLists];
}

static void registerItemToPasteboard(WebItemProviderRegistrationInfoList *representationsToRegister, id <AbstractPasteboard> pasteboard)
{
    registerItemsToPasteboard(@[ representationsToRegister ], pasteboard);
}

int64_t PlatformPasteboard::setColor(const Color& color)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);
    [representationsToRegister addData:[NSKeyedArchiver archivedDataWithRootObject:cocoaColor(color).get() requiringSecureCoding:NO error:nil] forType:UIColorPboardType];
    registerItemToPasteboard(representationsToRegister.get(), m_pasteboard.get());
    return 0;
}

static void addRepresentationsForPlainText(WebItemProviderRegistrationInfoList *itemsToRegister, const String& plainText)
{
    if (plainText.isEmpty())
        return;

#if HAVE(NSURL_ENCODING_INVALID_CHARACTERS)
    RetainPtr platformURL = adoptNS([[NSURL alloc] initWithString:plainText.createNSString().get() encodingInvalidCharacters:NO]);
#else
    RetainPtr platformURL = adoptNS([[NSURL alloc] initWithString:plainText.createNSString().get()]);
#endif

    if (URL(platformURL.get()).isValid())
        [itemsToRegister addRepresentingObject:platformURL.get()];

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    [itemsToRegister addData:[plainText.createNSString() dataUsingEncoding:NSUTF8StringEncoding] forType:bridge_cast(kUTTypeUTF8PlainText)];
ALLOW_DEPRECATED_DECLARATIONS_END
}

bool PlatformPasteboard::allowReadingURLAtIndex(const URL& url, int index) const
{
    NSItemProvider *itemProvider = (NSUInteger)index < [m_pasteboard itemProviders].count ? [[m_pasteboard itemProviders] objectAtIndex:index] : nil;
    for (NSString *type in itemProvider.registeredTypeIdentifiers) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if (UTTypeConformsTo((CFStringRef)type, kUTTypeURL))
            return true;
ALLOW_DEPRECATED_DECLARATIONS_END
    }

    return url.isValid();
}

void PlatformPasteboard::write(const PasteboardWebContent& content)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);

#if !PLATFORM(MACCATALYST)
    [representationsToRegister addData:[webIOSPastePboardType dataUsingEncoding:NSUTF8StringEncoding] forType:webIOSPastePboardType];
#endif

    for (size_t i = 0, size = content.clientTypesAndData.size(); i < size; ++i)
        [representationsToRegister addData:content.clientTypesAndData[i].second->makeContiguous()->createNSData().get() forType:content.clientTypesAndData[i].first.createNSString().get()];

    if (content.dataInWebArchiveFormat) {
        auto webArchiveData = content.dataInWebArchiveFormat->createNSData();
#if !PLATFORM(MACCATALYST)
        [representationsToRegister addData:webArchiveData.get() forType:WebArchivePboardType];
#endif
        [representationsToRegister addData:webArchiveData.get() forType:UTTypeWebArchive.identifier];
    }

    if (content.dataInAttributedStringFormat) {
        if (NSAttributedString *attributedString = [NSKeyedUnarchiver unarchivedObjectOfClasses:[NSSet setWithObject:NSAttributedString.class] fromData:content.dataInAttributedStringFormat->makeContiguous()->createNSData().get() error:nullptr])
            [representationsToRegister addRepresentingObject:attributedString];
    }

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (content.dataInRTFDFormat)
        [representationsToRegister addData:content.dataInRTFDFormat->createNSData().get() forType:bridge_cast(kUTTypeFlatRTFD)];

    if (content.dataInRTFFormat)
        [representationsToRegister addData:content.dataInRTFFormat->createNSData().get() forType:bridge_cast(kUTTypeRTF)];

    if (!content.dataInHTMLFormat.isEmpty()) {
        NSData *htmlAsData = [content.dataInHTMLFormat.createNSString() dataUsingEncoding:NSUTF8StringEncoding];
        [representationsToRegister addData:htmlAsData forType:bridge_cast(kUTTypeHTML)];
    }
ALLOW_DEPRECATED_DECLARATIONS_END

    if (!content.dataInStringFormat.isEmpty())
        addRepresentationsForPlainText(representationsToRegister.get(), content.dataInStringFormat);

    PasteboardCustomData customData;
    customData.setOrigin(content.contentOrigin);
    [representationsToRegister addData:customData.createSharedBuffer()->createNSData().get() forType:@(PasteboardCustomData::cocoaType().characters())];

    registerItemToPasteboard(representationsToRegister.get(), m_pasteboard.get());
}

void PlatformPasteboard::write(const PasteboardImage& pasteboardImage)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);

    for (size_t i = 0, size = pasteboardImage.clientTypesAndData.size(); i < size; ++i)
        [representationsToRegister addData:pasteboardImage.clientTypesAndData[i].second->createNSData().get() forType:pasteboardImage.clientTypesAndData[i].first.createNSString().get()];

    if (pasteboardImage.resourceData && !pasteboardImage.resourceMIMEType.isEmpty()) {
        auto utiOrMIMEType = pasteboardImage.resourceMIMEType;
        if (!isDeclaredUTI(utiOrMIMEType))
            utiOrMIMEType = UTIFromMIMEType(utiOrMIMEType);

        auto imageData = pasteboardImage.resourceData->makeContiguous()->createNSData();
        [representationsToRegister addData:imageData.get() forType:utiOrMIMEType.createNSString().get()];
        [representationsToRegister setPreferredPresentationSize:pasteboardImage.imageSize];
        [representationsToRegister setSuggestedName:pasteboardImage.suggestedName.createNSString().get()];
    }

    // FIXME: When writing a PasteboardImage, we currently always place the image data at a higer fidelity than the
    // associated image URL. However, in the case of an image enclosed by an anchor, we might want to consider the
    // the URL (i.e. the anchor's href attribute) to be a higher fidelity representation.
    auto& pasteboardURL = pasteboardImage.url;
    if (RetainPtr nsURL = pasteboardURL.url.createNSURL()) {
#if HAVE(NSURL_TITLE)
        [nsURL _web_setTitle:pasteboardURL.title.isEmpty() ? WTF::userVisibleString(pasteboardURL.url.createNSURL().get()) : pasteboardURL.title.createNSString().get()];
#endif
        [representationsToRegister addRepresentingObject:nsURL.get()];
    }

    registerItemToPasteboard(representationsToRegister.get(), m_pasteboard.get());
}

void PlatformPasteboard::write(const String& pasteboardType, const String& text)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);
    [representationsToRegister setPreferredPresentationStyle:WebPreferredPresentationStyleInline];

    RetainPtr pasteboardTypeAsNSString = pasteboardType.createNSString();
    if (!text.isEmpty() && pasteboardTypeAsNSString.get().length) {
        RetainPtr pasteboardTypeAsCFString = bridge_cast(pasteboardTypeAsNSString.get());
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if (UTTypeConformsTo(pasteboardTypeAsCFString.get(), kUTTypeURL) || UTTypeConformsTo(pasteboardTypeAsCFString.get(), kUTTypeText))
            addRepresentationsForPlainText(representationsToRegister.get(), text);
        else
            [representationsToRegister addData:[pasteboardTypeAsNSString dataUsingEncoding:NSUTF8StringEncoding] forType:pasteboardType.createNSString().get()];
ALLOW_DEPRECATED_DECLARATIONS_END
    }

    registerItemToPasteboard(representationsToRegister.get(), m_pasteboard.get());
}

void PlatformPasteboard::write(const PasteboardURL& url)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);
    [representationsToRegister setPreferredPresentationStyle:WebPreferredPresentationStyleInline];

    if (RetainPtr nsURL = url.url.createNSURL()) {
#if HAVE(NSURL_TITLE)
        if (!url.title.isEmpty())
            [nsURL _web_setTitle:url.title.createNSString().get()];
#endif
        [representationsToRegister addRepresentingObject:nsURL.get()];
        [representationsToRegister addRepresentingObject:url.url.string().createNSString().get()];
    }

    registerItemToPasteboard(representationsToRegister.get(), m_pasteboard.get());
}

static const char originKeyForTeamData[] = "com.apple.WebKit.drag-and-drop-team-data.origin";
static const char customTypesKeyForTeamData[] = "com.apple.WebKit.drag-and-drop-team-data.custom-types";

Vector<String> PlatformPasteboard::typesSafeForDOMToReadAndWrite(const String& origin) const
{
    ListHashSet<String> domPasteboardTypes;
#if PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA
    for (NSItemProvider *provider in [m_pasteboard itemProviders]) {
        if (!provider.teamData.length)
            continue;

        NSSet *classes = [NSSet setWithObjects:NSDictionary.class, NSString.class, NSArray.class, nil];
        id teamDataObject = [NSKeyedUnarchiver unarchivedObjectOfClasses:classes fromData:provider.teamData error:nullptr];
        if (![teamDataObject isKindOfClass:NSDictionary.class])
            continue;

        RetainPtr originInTeamData = dynamic_objc_cast<NSString>([teamDataObject objectForKey:@(originKeyForTeamData)]);
        if (!originInTeamData)
            continue;
        if (String(originInTeamData.get()) != origin)
            continue;

        id customTypes = [teamDataObject objectForKey:@(customTypesKeyForTeamData)];
        if (![customTypes isKindOfClass:NSArray.class])
            continue;

        for (NSString *type in customTypes)
            domPasteboardTypes.add(type);
    }
#endif // PASTEBOARD_SUPPORTS_PRESENTATION_STYLE_AND_TEAM_DATA

    if (NSData *serializedCustomData = [m_pasteboard dataForPasteboardType:@(PasteboardCustomData::cocoaType().characters())]) {
        auto data = PasteboardCustomData::fromSharedBuffer(SharedBuffer::create(serializedCustomData).get());
        if (data.origin() == origin) {
            for (auto& type : data.orderedTypes())
                domPasteboardTypes.add(type);
        }
    }

    auto webSafePasteboardTypes = webSafeTypes([m_pasteboard pasteboardTypes], IncludeImageTypes::No, [&] {
        BOOL ableToDetermineProtocolOfPasteboardURL = ![m_pasteboard isKindOfClass:[WebItemProviderPasteboard class]];
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        return ableToDetermineProtocolOfPasteboardURL && stringForType(kUTTypeURL).isEmpty();
ALLOW_DEPRECATED_DECLARATIONS_END
    });

    for (auto& type : webSafePasteboardTypes)
        domPasteboardTypes.add(type);

    return copyToVector(domPasteboardTypes);
}

static RetainPtr<WebItemProviderRegistrationInfoList> createItemProviderRegistrationList(const PasteboardCustomData& data)
{
    auto representationsToRegister = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);
    [representationsToRegister setPreferredPresentationStyle:WebPreferredPresentationStyleInline];

    if (data.hasSameOriginCustomData() || !data.origin().isEmpty()) {
        if (auto serializedSharedBuffer = data.createSharedBuffer()->createNSData()) {
            // We stash the list of supplied pasteboard types in teamData here for compatibility with drag and drop.
            // Since the contents of item providers cannot be loaded prior to drop, but the pasteboard types are
            // contained within the custom data blob and we need to vend them to the page when firing `dragover`
            // events, we need an additional in-memory representation of the pasteboard types array that contains
            // all of the custom types. We use the teamData property, available on NSItemProvider on iOS, to store
            // this information, since the contents of teamData are immediately available prior to the drop.
            NSDictionary *teamDataDictionary = @{ @(originKeyForTeamData) : data.origin().createNSString().get(), @(customTypesKeyForTeamData) : createNSArray(data.orderedTypes()).get() };
            if (NSData *teamData = [NSKeyedArchiver archivedDataWithRootObject:teamDataDictionary requiringSecureCoding:YES error:nullptr]) {
                [representationsToRegister setTeamData:teamData];
                [representationsToRegister addData:serializedSharedBuffer.get() forType:@(PasteboardCustomData::cocoaType().characters())];
            }
        }
    }

    data.forEachPlatformStringOrBuffer([&] (auto& type, auto& value) {
        auto cocoaType = PlatformPasteboard::platformPasteboardTypeForSafeTypeForDOMToReadAndWrite(type, PlatformPasteboard::IncludeImageTypes::Yes);
        if (cocoaType.isEmpty())
            return;

        if (std::holds_alternative<String>(value)) {
            if (std::get<String>(value).isNull())
                return;

            RetainPtr nsStringValue = std::get<String>(value).createNSString();
            auto cfType = cocoaType.createCFString();
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if (UTTypeConformsTo(cfType.get(), kUTTypeURL))
                [representationsToRegister addRepresentingObject:adoptNS([[NSURL alloc] initWithString:nsStringValue.get()]).get()];
            else if (UTTypeConformsTo(cfType.get(), kUTTypePlainText))
                [representationsToRegister addRepresentingObject:nsStringValue.get()];
            else
                [representationsToRegister addData:[nsStringValue dataUsingEncoding:NSUTF8StringEncoding] forType:cocoaType.createNSString().get()];
ALLOW_DEPRECATED_DECLARATIONS_END
            return;
        }

        auto buffer = std::get<Ref<SharedBuffer>>(value);
        [representationsToRegister addData:buffer->createNSData().get() forType:cocoaType.createNSString().get()];
    });

    return representationsToRegister;
}

int64_t PlatformPasteboard::write(const Vector<PasteboardCustomData>& itemData, PasteboardDataLifetime)
{
    auto registrationLists = createNSArray(itemData, [] (auto& data) {
        return createItemProviderRegistrationList(data);
    });
    registerItemsToPasteboard(registrationLists.get(), m_pasteboard.get());
    return [m_pasteboard changeCount];
}

#else

int64_t PlatformPasteboard::setColor(const Color&)
{
    return 0;
}

bool PlatformPasteboard::allowReadingURLAtIndex(const URL&, int) const
{
    return false;
}

void PlatformPasteboard::write(const PasteboardWebContent&)
{
}

void PlatformPasteboard::write(const PasteboardImage&)
{
}

void PlatformPasteboard::write(const String&, const String&)
{
}

void PlatformPasteboard::write(const PasteboardURL&)
{
}

Vector<String> PlatformPasteboard::typesSafeForDOMToReadAndWrite(const String&) const
{
    return { };
}

int64_t PlatformPasteboard::write(const Vector<PasteboardCustomData>&, PasteboardDataLifetime)
{
    return 0;
}

#endif

int PlatformPasteboard::count() const
{
    return [m_pasteboard numberOfItems];
}

Vector<String> PlatformPasteboard::allStringsForType(const String& type) const
{
    auto numberOfItems = count();
    Vector<String> strings;
    strings.reserveInitialCapacity(numberOfItems);
    for (int index = 0; index < numberOfItems; ++index) {
        String value = readString(index, type);
        if (!value.isEmpty())
            strings.append(WTFMove(value));
    }
    return strings;
}

static bool isDisallowedTypeForReadBuffer(NSString *type)
{
    return [type isEqualToString:UIImagePboardType];
}

RefPtr<SharedBuffer> PlatformPasteboard::readBuffer(std::optional<size_t> index, const String& type) const
{
    RetainPtr nsType = type.createNSString();
    if (isDisallowedTypeForReadBuffer(nsType.get()))
        return nullptr;

    NSInteger integerIndex = index.value_or(0);
    if (integerIndex < 0 || integerIndex >= [m_pasteboard numberOfItems])
        return nullptr;

    NSIndexSet *indexSet = [NSIndexSet indexSetWithIndex:integerIndex];

    RetainPtr<NSArray> pasteboardItem = [m_pasteboard dataForPasteboardType:nsType.get() inItemSet:indexSet];

    if (![pasteboardItem count])
        return nullptr;

    if (NSData *data = [pasteboardItem firstObject])
        return SharedBuffer::create(data);
    return nullptr;
}

String PlatformPasteboard::readString(size_t index, const String& type) const
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (type == String(kUTTypeURL)) {
        String title;
        return [readURL(index, title).createNSURL() absoluteString];
    }

    if ((NSInteger)index < 0 || (NSInteger)index >= [m_pasteboard numberOfItems])
        return { };

    NSIndexSet *indexSet = [NSIndexSet indexSetWithIndex:index];
    RetainPtr value = [m_pasteboard valuesForPasteboardType:type.createNSString().get() inItemSet:indexSet].firstObject ?: [m_pasteboard dataForPasteboardType:type.createNSString().get() inItemSet:indexSet].firstObject;
    if (!value)
        return { };

    if ([value isKindOfClass:[NSData class]])
        value = adoptNS([[NSString alloc] initWithData:(NSData *)value.get() encoding:NSUTF8StringEncoding]);
    
    if (type == String(kUTTypePlainText) || type == String(kUTTypeHTML)) {
        ASSERT([value isKindOfClass:[NSString class]]);
        return [value isKindOfClass:[NSString class]] ? value.get() : nil;
    }
    if (type == String(kUTTypeText)) {
        ASSERT([value isKindOfClass:[NSString class]] || [value isKindOfClass:[NSAttributedString class]]);
        if ([value isKindOfClass:[NSString class]])
            return value.get();
        if ([value isKindOfClass:[NSAttributedString class]])
            return [(NSAttributedString *)value string];
    }
ALLOW_DEPRECATED_DECLARATIONS_END

    return String();
}

URL PlatformPasteboard::readURL(size_t index, String& title) const
{
    if ((NSInteger)index < 0 || (NSInteger)index >= [m_pasteboard numberOfItems])
        return { };

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    id value = [m_pasteboard valuesForPasteboardType:(__bridge NSString *)kUTTypeURL inItemSet:[NSIndexSet indexSetWithIndex:index]].firstObject;
ALLOW_DEPRECATED_DECLARATIONS_END
    if (!value)
        return { };

    RetainPtr url = dynamic_objc_cast<NSURL>(value);
    if (!url) {
        if (auto *urlData = dynamic_objc_cast<NSData>(value))
            url = adoptNS([[NSURL alloc] initWithDataRepresentation:urlData relativeToURL:nil]);
    }

    if (!url)
        return { };

    if (!allowReadingURLAtIndex(url.get(), index))
        return { };

#if PASTEBOARD_SUPPORTS_ITEM_PROVIDERS && HAVE(NSURL_TITLE)
    title = [url _web_title];
#else
    UNUSED_PARAM(title);
#endif

    return url.get();
}

void PlatformPasteboard::updateSupportedTypeIdentifiers(const Vector<String>& types)
{
    if (![m_pasteboard respondsToSelector:@selector(updateSupportedTypeIdentifiers:)])
        return;

    [m_pasteboard updateSupportedTypeIdentifiers:createNSArray(types).get()];
}

int64_t PlatformPasteboard::write(const PasteboardCustomData& data, PasteboardDataLifetime)
{
    return write(Vector<PasteboardCustomData> { data });
}

bool PlatformPasteboard::containsURLStringSuitableForLoading()
{
    Vector<String> types;
    getTypes(types);
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (!types.contains(String(kUTTypeURL)))
        return false;

    auto urlString = stringForType(kUTTypeURL);
ALLOW_DEPRECATED_DECLARATIONS_END
    if (urlString.isEmpty()) {
        // On iOS, we don't get access to the contents of NSItemProviders until we perform the drag operation.
        // Thus, we consider DragData to contain an URL if it contains the `public.url` UTI type. Later down the
        // road, when we perform the drag operation, we can then check if the URL's protocol is http or https,
        // and if it isn't, we bail out of page navigation.
        return true;
    }
    return URL { adoptNS([[NSURL alloc] initWithString:urlString.createNSString().get()]).get() }.protocolIsInHTTPFamily();
}

}

#endif // PLATFORM(IOS_FAMILY)
