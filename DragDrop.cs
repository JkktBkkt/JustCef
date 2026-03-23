namespace JustCef;

[Flags]
public enum JustCefDragOperationsMask : uint
{
    None = 0,
    Copy = 1,
    Link = 2,
    Generic = 4,
    Private = 8,
    Move = 16,
    Delete = 32,
    Every = uint.MaxValue
}

public enum JustCefDragContentKind : byte
{
    LinkUrl = 1,
    LinkTitle = 2,
    LinkMetadata = 3,
    FragmentText = 4,
    FragmentHtml = 5,
    FragmentBaseUrl = 6,
    FilePath = 7
}

public readonly record struct JustCefDragPosition(
    int ClientX,
    int ClientY,
    int ScreenX,
    int ScreenY);

public readonly record struct JustCefDragContent(
    JustCefDragContentKind Kind,
    string Value);

public sealed class JustCefDragData
{
    public required JustCefDragOperationsMask AllowedOperations { get; init; }
    public required bool IsReadOnly { get; init; }
    public required bool HasImage { get; init; }
    public required IReadOnlyList<JustCefDragContent> Contents { get; init; }
    public JustCefDragPosition? Position { get; init; }

    public string? GetValue(JustCefDragContentKind kind)
    {
        foreach (var content in Contents)
        {
            if (content.Kind == kind)
                return content.Value;
        }

        return null;
    }

    public IReadOnlyList<string> GetValues(JustCefDragContentKind kind)
    {
        var values = new List<string>();
        foreach (var content in Contents)
        {
            if (content.Kind == kind)
                values.Add(content.Value);
        }

        return values;
    }
}
