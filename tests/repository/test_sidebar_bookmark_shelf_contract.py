import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SIDEBAR = ROOT / "overlay/chromium/src/ahoi/browser/ui/sidebar"


def text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


class SidebarBookmarkShelfContractTest(unittest.TestCase):
    def test_bookmarks_stay_separate_from_saved_and_temporary_tabs(self):
        host = text(SIDEBAR / "browser_sidebar_host_layout.cc")
        bookmark_shelf = host.index(
            "AddChildView(CreateSidebarBookmarkShelfView(browser_))"
        )
        tabs_surface = host.index("auto tabs_surface = CreateSidebarTabsSurfaceView()")
        self.assertLess(bookmark_shelf, tabs_surface)

    def test_shelf_uses_merged_bookmarks_and_horizontal_native_scrolling(self):
        shelf = text(SIDEBAR / "sidebar_bookmark_shelf_view.cc")
        self.assertIn("BookmarkMergedSurfaceServiceFactory::GetForProfile", shelf)
        self.assertIn("ScrollBarMode::kHiddenButEnabled", shelf)
        self.assertIn("SetTreatAllScrollEventsAsHorizontal(true)", shelf)
        self.assertIn("ax::mojom::Role::kToolbar", shelf)
        self.assertIn("SetHasPopup(ax::mojom::HasPopup::kMenu)", shelf)
        self.assertIn("chrome::ShowBookmarkManager", shelf)

    def test_folder_menu_is_recursive_and_has_guarded_open_all(self):
        menu = text(SIDEBAR / "sidebar_bookmark_menu.cc")
        self.assertIn("PopulateFolder(BookmarkParentFolder::FromFolderNode(node)", menu)
        self.assertIn("AddSubMenuWithIcon", menu)
        self.assertIn("IDS_BOOKMARK_BAR_OPEN_ALL_COUNT", menu)
        self.assertIn("WindowOpenDisposition::NEW_BACKGROUND_TAB", menu)
        self.assertIn("bookmarks::OpenAllIfAllowed", menu)

    def test_component_files_respect_the_line_budget(self):
        files = [
            SIDEBAR / "sidebar_bookmark_menu.cc",
            SIDEBAR / "sidebar_bookmark_menu.h",
            SIDEBAR / "sidebar_bookmark_shelf_view.cc",
            SIDEBAR / "sidebar_bookmark_shelf_view.h",
            SIDEBAR / "sidebar_bookmark_shelf_view_unittest.cc",
        ]
        for path in files:
            with self.subTest(path=path.name):
                self.assertLessEqual(len(text(path).splitlines()), 800)


if __name__ == "__main__":
    unittest.main()
