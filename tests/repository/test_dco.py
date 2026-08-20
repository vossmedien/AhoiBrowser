import unittest


from tools.check_dco import Commit, has_author_signoff


class DCOTests(unittest.TestCase):
    def commit(self, message: str) -> Commit:
        return Commit(
            sha="a" * 40,
            author_name="Ahoi Contributor",
            author_email="contributor@example.invalid",
            message=message,
        )

    def test_matching_author_signoff_passes(self):
        commit = self.commit(
            "Implement feature\n\nSigned-off-by: Ahoi Contributor "
            "<contributor@example.invalid>\n"
        )
        self.assertTrue(has_author_signoff(commit))

    def test_missing_or_different_author_signoff_fails(self):
        self.assertFalse(has_author_signoff(self.commit("Unsigned\n")))
        self.assertFalse(
            has_author_signoff(
                self.commit(
                    "Wrong signer\n\nSigned-off-by: Someone Else "
                    "<else@example.invalid>\n"
                )
            )
        )

    def test_trailer_is_case_insensitive_but_exact(self):
        commit = self.commit(
            "Case\n\nsigned-off-by: ahoi contributor "
            "<CONTRIBUTOR@example.invalid>\n"
        )
        self.assertTrue(has_author_signoff(commit))


if __name__ == "__main__":
    unittest.main()
