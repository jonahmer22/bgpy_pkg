from yamlable import YamlAble, yaml_info

# import the C extension
from ._announcement_c import Announcement as _CAnnouncement

# create a class from the C extension portion of the code and the C extension
@yaml_info(yaml_tag="Announcement")
class Announcement(_CAnnouncement, YamlAble):
    """BGP Announcement"""