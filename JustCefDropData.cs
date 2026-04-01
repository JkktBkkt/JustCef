namespace JustCef;

public sealed record JustCefDropData(
    int OperationsMask,
    bool IsFile,
    bool IsLink,
    bool IsFragment,
    string[] FilePaths,
    string[] FileNames,
    string? LinkUrl,
    string? LinkTitle,
    string? LinkMetadata,
    string? FragmentText,
    string? FragmentHtml,
    string? FragmentBaseUrl);
