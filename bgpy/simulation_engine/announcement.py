from yamlable import YamlAble, yaml_info

from ._announcement_c import Announcement as _CAnnouncement

@yaml_info(yaml_tag="Announcement")
class Announcement(_CAnnouncement, YamlAble):
    """BGP Announcement"""
